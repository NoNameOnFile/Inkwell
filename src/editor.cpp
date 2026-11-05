#include "editor.hpp"

#include "utf8.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace inkwell {
namespace {

constexpr size_t kUndoLimit = 300;

bool is_wordish(unsigned char c) {
    return std::isalnum(c) != 0 || c >= 128 || c == '\'' || c == '-';
}

std::string strip_returns(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c != '\r') {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

Editor::Editor() {
    saved_body_.clear();
}

void Editor::assign_text(std::string text) {
    text = strip_returns(std::move(text));
    lines_.clear();
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines_.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    lines_.push_back(std::move(current));
    if (lines_.empty()) {
        lines_.emplace_back();
    }
    line_ = 0;
    col_ = 0;
    mark_on_ = false;
    clamp_cursor();
}

void Editor::clamp_cursor() {
    if (lines_.empty()) {
        lines_.emplace_back();
    }
    if (line_ < 0) {
        line_ = 0;
    }
    if (line_ >= static_cast<int>(lines_.size())) {
        line_ = static_cast<int>(lines_.size()) - 1;
    }
    if (col_ < 0) {
        col_ = 0;
    }
    if (col_ > static_cast<int>(lines_[static_cast<size_t>(line_)].size())) {
        col_ = static_cast<int>(lines_[static_cast<size_t>(line_)].size());
    }
    const std::string& current = lines_[static_cast<size_t>(line_)];
    while (col_ > 0 && (static_cast<unsigned char>(current[static_cast<size_t>(col_)]) & 0xC0) == 0x80) {
        --col_;
    }
}

void Editor::note_saved() {
    saved_body_ = text();
    dirty_ = false;
}

std::string Editor::text() const {
    std::string out;
    size_t bytes = 0;
    for (const std::string& line : lines_) {
        bytes += line.size() + 1;
    }
    out.reserve(bytes);
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i != 0) {
            out.push_back('\n');
        }
        out += lines_[i];
    }
    return out;
}

size_t Editor::offset_of(int at_line, int at_col) const {
    if (lines_.empty()) {
        return 0;
    }
    if (at_line < 0) {
        at_line = 0;
    }
    if (at_line >= static_cast<int>(lines_.size())) {
        at_line = static_cast<int>(lines_.size()) - 1;
    }
    size_t offset = 0;
    for (int i = 0; i < at_line; ++i) {
        offset += lines_[static_cast<size_t>(i)].size() + 1;
    }
    const auto& line = lines_[static_cast<size_t>(at_line)];
    if (at_col < 0) {
        at_col = 0;
    }
    if (at_col > static_cast<int>(line.size())) {
        at_col = static_cast<int>(line.size());
    }
    return offset + static_cast<size_t>(at_col);
}

std::pair<int, int> Editor::cursor_at(size_t offset) const {
    size_t passed = 0;
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        const size_t len = lines_[static_cast<size_t>(i)].size();
        if (offset <= passed + len) {
            return {i, static_cast<int>(offset - passed)};
        }
        passed += len + 1;
    }
    if (lines_.empty()) {
        return {0, 0};
    }
    return {static_cast<int>(lines_.size()) - 1, static_cast<int>(lines_.back().size())};
}

size_t Editor::cursor_offset() const {
    return offset_of(line_, col_);
}

bool Editor::has_selection() const {
    return mark_on_ && (mark_line_ != line_ || mark_col_ != col_);
}

std::pair<size_t, size_t> Editor::selection_bounds() const {
    const size_t a = offset_of(mark_line_, mark_col_);
    const size_t b = offset_of(line_, col_);
    if (a < b) {
        return {a, b};
    }
    return {b, a};
}

std::string Editor::selection_text() const {
    if (!has_selection()) {
        return {};
    }
    const auto [begin, end] = selection_bounds();
    const std::string body = text();
    return body.substr(begin, end - begin);
}

void Editor::toggle_mark() {
    if (mark_on_ && mark_line_ == line_ && mark_col_ == col_) {
        mark_on_ = false;
        return;
    }
    if (mark_on_) {
        mark_on_ = false;
        return;
    }
    mark_on_ = true;
    mark_line_ = line_;
    mark_col_ = col_;
}

void Editor::clear_mark() {
    mark_on_ = false;
}

Editor::Snapshot Editor::capture() const {
    Snapshot shot;
    shot.lines = lines_;
    shot.line = line_;
    shot.col = col_;
    shot.mark_on = mark_on_;
    shot.mark_line = mark_line_;
    shot.mark_col = mark_col_;
    return shot;
}

void Editor::restore(const Snapshot& snapshot) {
    lines_ = snapshot.lines;
    line_ = snapshot.line;
    col_ = snapshot.col;
    mark_on_ = snapshot.mark_on;
    mark_line_ = snapshot.mark_line;
    mark_col_ = snapshot.mark_col;
    clamp_cursor();
    dirty_ = text() != saved_body_;
}

void Editor::push_undo() {
    undo_.push_back(capture());
    if (undo_.size() > kUndoLimit) {
        undo_.erase(undo_.begin());
    }
}

void Editor::before_type(bool coalesce) {
    if (coalesce && last_action_ == Action::Type) {
        return;
    }
    push_undo();
    redo_.clear();
    last_action_ = Action::Type;
}

void Editor::before_delete() {
    if (last_action_ == Action::Delete) {
        return;
    }
    push_undo();
    redo_.clear();
    last_action_ = Action::Delete;
}

void Editor::before_other() {
    push_undo();
    redo_.clear();
    last_action_ = Action::Other;
}

void Editor::erase_selection() {
    if (!has_selection()) {
        return;
    }
    const auto [begin, end] = selection_bounds();
    std::string body = text();
    body.erase(begin, end - begin);
    assign_text(body);
    const auto [line, col] = cursor_at(begin);
    line_ = line;
    col_ = col;
    mark_on_ = false;
    clamp_cursor();
    dirty_ = true;
}

void Editor::insert(std::string text) {
    text = strip_returns(std::move(text));
    if (text.empty() && !has_selection()) {
        return;
    }
    const bool one_codepoint = utf8_count(text) == 1 && text.find('\n') == std::string::npos;
    bool boundary = false;
    if (one_codepoint) {
        const unsigned char c = static_cast<unsigned char>(text[0]);
        boundary = c == ' ' || c == '\t' || std::ispunct(c) != 0;
    }
    if (has_selection() || !one_codepoint || boundary) {
        before_other();
    } else {
        before_type(true);
    }
    if (has_selection()) {
        erase_selection();
    }
    size_t cursor = 0;
    while (cursor < text.size()) {
        const size_t nl = text.find('\n', cursor);
        const size_t end = nl == std::string::npos ? text.size() : nl;
        const std::string piece = text.substr(cursor, end - cursor);
        lines_[static_cast<size_t>(line_)].insert(static_cast<size_t>(col_), piece);
        col_ += static_cast<int>(piece.size());
        if (nl == std::string::npos) {
            break;
        }
        const std::string rest = lines_[static_cast<size_t>(line_)].substr(static_cast<size_t>(col_));
        lines_[static_cast<size_t>(line_)].resize(static_cast<size_t>(col_));
        lines_.insert(lines_.begin() + line_ + 1, rest);
        ++line_;
        col_ = 0;
        cursor = nl + 1;
        if (cursor == text.size()) {
            break;
        }
    }
    dirty_ = true;
    clamp_cursor();
}

void Editor::newline() {
    before_other();
    if (has_selection()) {
        erase_selection();
    }
    const std::string rest = lines_[static_cast<size_t>(line_)].substr(static_cast<size_t>(col_));
    lines_[static_cast<size_t>(line_)].resize(static_cast<size_t>(col_));
    lines_.insert(lines_.begin() + line_ + 1, rest);
    ++line_;
    col_ = 0;
    dirty_ = true;
}

void Editor::backspace() {
    if (has_selection()) {
        before_other();
        erase_selection();
        return;
    }
    if (col_ == 0 && line_ == 0) {
        return;
    }
    before_delete();
    if (col_ == 0) {
        const std::string rest = lines_[static_cast<size_t>(line_)];
        lines_.erase(lines_.begin() + line_);
        --line_;
        col_ = static_cast<int>(lines_[static_cast<size_t>(line_)].size());
        lines_[static_cast<size_t>(line_)] += rest;
    } else {
        const size_t prev = utf8_prev(lines_[static_cast<size_t>(line_)], static_cast<size_t>(col_));
        lines_[static_cast<size_t>(line_)].erase(prev, static_cast<size_t>(col_) - prev);
        col_ = static_cast<int>(prev);
    }
    dirty_ = true;
    clamp_cursor();
}

void Editor::delete_forward() {
    if (has_selection()) {
        before_other();
        erase_selection();
        return;
    }
    const std::string& current = lines_[static_cast<size_t>(line_)];
    if (col_ >= static_cast<int>(current.size()) && line_ + 1 >= static_cast<int>(lines_.size())) {
        return;
    }
    before_delete();
    if (col_ >= static_cast<int>(lines_[static_cast<size_t>(line_)].size())) {
        lines_[static_cast<size_t>(line_)] += lines_[static_cast<size_t>(line_) + 1];
        lines_.erase(lines_.begin() + line_ + 1);
    } else {
        int length = 1;
        utf8_decode(lines_[static_cast<size_t>(line_)], static_cast<size_t>(col_), length);
        if (length < 1) {
            length = 1;
        }
        lines_[static_cast<size_t>(line_)].erase(static_cast<size_t>(col_), static_cast<size_t>(length));
    }
    dirty_ = true;
    clamp_cursor();
}

void Editor::move_left() {
    last_action_ = Action::None;
    if (col_ == 0) {
        if (line_ > 0) {
            --line_;
            col_ = static_cast<int>(lines_[static_cast<size_t>(line_)].size());
        }
        return;
    }
    col_ = static_cast<int>(utf8_prev(lines_[static_cast<size_t>(line_)], static_cast<size_t>(col_)));
}

void Editor::move_right() {
    last_action_ = Action::None;
    const std::string& current = lines_[static_cast<size_t>(line_)];
    if (col_ >= static_cast<int>(current.size())) {
        if (line_ + 1 < static_cast<int>(lines_.size())) {
            ++line_;
            col_ = 0;
        }
        return;
    }
    int length = 1;
    utf8_decode(current, static_cast<size_t>(col_), length);
    if (length < 1) {
        length = 1;
    }
    col_ += length;
}

void Editor::move_home() {
    last_action_ = Action::None;
    col_ = 0;
}

void Editor::move_end() {
    last_action_ = Action::None;
    col_ = static_cast<int>(lines_[static_cast<size_t>(line_)].size());
}

void Editor::move_word_left() {
    last_action_ = Action::None;
    if (col_ == 0) {
        move_left();
        return;
    }
    const std::string& current = lines_[static_cast<size_t>(line_)];
    while (col_ > 0) {
        const size_t prev = utf8_prev(current, static_cast<size_t>(col_));
        if (is_wordish(static_cast<unsigned char>(current[prev]))) {
            break;
        }
        col_ = static_cast<int>(prev);
    }
    while (col_ > 0) {
        const size_t prev = utf8_prev(current, static_cast<size_t>(col_));
        if (!is_wordish(static_cast<unsigned char>(current[prev]))) {
            break;
        }
        col_ = static_cast<int>(prev);
    }
}

void Editor::move_word_right() {
    last_action_ = Action::None;
    const std::string& current = lines_[static_cast<size_t>(line_)];
    if (col_ >= static_cast<int>(current.size())) {
        move_right();
        return;
    }
    auto step = [&]() {
        int length = 1;
        utf8_decode(current, static_cast<size_t>(col_), length);
        if (length < 1) {
            length = 1;
        }
        col_ += length;
    };
    while (col_ < static_cast<int>(current.size()) &&
           !is_wordish(static_cast<unsigned char>(current[static_cast<size_t>(col_)]))) {
        step();
    }
    while (col_ < static_cast<int>(current.size()) &&
           is_wordish(static_cast<unsigned char>(current[static_cast<size_t>(col_)]))) {
        step();
    }
}

void Editor::move_document_start() {
    last_action_ = Action::None;
    line_ = 0;
    col_ = 0;
}

void Editor::move_document_end() {
    last_action_ = Action::None;
    line_ = static_cast<int>(lines_.size()) - 1;
    col_ = static_cast<int>(lines_.back().size());
}

void Editor::set_cursor(int next_line, int next_col) {
    last_action_ = Action::None;
    line_ = next_line;
    col_ = next_col;
    clamp_cursor();
}

void Editor::undo() {
    if (undo_.empty()) {
        return;
    }
    redo_.push_back(capture());
    restore(undo_.back());
    undo_.pop_back();
    last_action_ = Action::None;
}

void Editor::redo() {
    if (redo_.empty()) {
        return;
    }
    undo_.push_back(capture());
    restore(redo_.back());
    redo_.pop_back();
    last_action_ = Action::None;
}

bool Editor::load_file(const std::string& file, std::string& error) {
    std::ifstream in(std::filesystem::path(file), std::ios::binary);
    if (!in) {
        error = "Could not open " + file;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string body = strip_returns(buffer.str());
    if (!body.empty() && body.back() == '\n') {
        body.pop_back();
    }
    assign_text(body);
    path_ = file;
    undo_.clear();
    redo_.clear();
    last_action_ = Action::None;
    note_saved();
    return true;
}

bool Editor::save_file(const std::string& file, std::string& error) {
    std::ofstream out(std::filesystem::path(file), std::ios::binary);
    if (!out) {
        error = "Could not write " + file;
        return false;
    }
    const std::string body = text();
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.put('\n');
    if (!out) {
        error = "Could not finish writing " + file;
        return false;
    }
    path_ = file;
    note_saved();
    return true;
}

void Editor::replace_span(size_t begin, size_t end, const std::string& replacement) {
    std::string body = text();
    if (begin > body.size()) {
        begin = body.size();
    }
    if (end > body.size()) {
        end = body.size();
    }
    if (end < begin) {
        end = begin;
    }
    before_other();
    body.replace(begin, end - begin, replacement);
    assign_text(body);
    const auto [line, col] = cursor_at(begin + replacement.size());
    line_ = line;
    col_ = col;
    mark_on_ = false;
    clamp_cursor();
    dirty_ = text() != saved_body_;
}

}  // namespace inkwell
