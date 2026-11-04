#include "app.hpp"

#include "editor.hpp"
#include "grammar.hpp"
#include "layout.hpp"
#include "plugin.hpp"
#include "spell.hpp"
#include "term.hpp"
#include "text.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cctype>
#include <clocale>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>

namespace inkwell {
namespace {

enum class Mode { Edit, Command, Preview, Help };

struct Metrics {
    int cols = 80;
    int rows = 24;
    int gutter = 4;
    int text_width = 76;
    int editor_rows = 16;
    int panel_top = 17;
    int body_rows = 6;
    int hint_row = 23;
};

struct App {
    Editor editor;
    Dictionary dictionary;
    std::vector<Issue> issues;
    std::string status;
    std::string dict_error;
    Mode mode = Mode::Edit;
    std::string command;
    size_t command_col = 0;
    std::string last_command;
    std::string preview;
    size_t preview_begin = 0;
    size_t preview_end = 0;
    int preview_scroll = 0;
    int help_scroll = 0;
    int scroll = 0;
    int panel_scroll = 0;
    int preferred_col = 0;
    bool analyze = true;
    bool quit_armed = false;
    bool running = true;
    bool composing = false;
};

struct CommandSpec {
    const char* name;
    const char* help;
};

const CommandSpec kCommands[] = {
    {"learn", "Remember the word under the cursor"},
    {"ignore", "Ignore that word for this session"},
    {"save", "save [path]"},
    {"open", "open <path>"},
    {"copy", "Copy the selection"},
    {"help", "Show the keys"},
    {"quit", "Leave Inkwell"},
};

std::string base64_encode(std::string_view input) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    unsigned value = 0;
    int bits = 0;
    for (unsigned char c : input) {
        value = (value << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(table[(value >> bits) & 0x3F]);
        }
    }
    if (bits > 0) {
        out.push_back(table[(value << (6 - bits)) & 0x3F]);
    }
    while (out.size() % 4 != 0) {
        out.push_back('=');
    }
    return out;
}

std::vector<size_t> line_offsets(const std::vector<std::string>& lines) {
    std::vector<size_t> offsets(lines.size());
    size_t cursor = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        offsets[i] = cursor;
        cursor += lines[i].size() + 1;
    }
    return offsets;
}

Metrics measure(int cols, int rows, int line_count) {
    Metrics metrics;
    metrics.cols = std::max(2, cols);
    metrics.rows = std::max(2, rows);
    int digits = 1;
    int count = std::max(1, line_count);
    while (count >= 10) {
        count /= 10;
        ++digits;
    }
    metrics.gutter = std::max(4, digits + 1);
    if (metrics.cols < metrics.gutter + 8) {
        metrics.gutter = 0;
    }
    metrics.text_width = std::max(1, metrics.cols - metrics.gutter);
    int panel_rows = std::min(11, std::max(5, metrics.rows / 3));
    int editor_rows = metrics.rows - panel_rows - 1;
    if (editor_rows < 1) {
        editor_rows = 1;
        panel_rows = std::max(1, metrics.rows - 2);
    }
    metrics.editor_rows = editor_rows;
    metrics.panel_top = editor_rows + 1;
    metrics.hint_row = metrics.rows - 1;
    metrics.body_rows = std::max(0, metrics.hint_row - metrics.panel_top - 1);
    return metrics;
}

int active_issue(const App& app) {
    const size_t offset = app.editor.cursor_offset();
    int inside = -1;
    int edge = -1;
    for (int i = 0; i < static_cast<int>(app.issues.size()); ++i) {
        const Issue& issue = app.issues[static_cast<size_t>(i)];
        if (offset >= issue.begin && offset < issue.end) {
            inside = i;
            break;
        }
        if (offset == issue.end) {
            edge = i;
        }
    }
    if (inside >= 0) {
        return inside;
    }
    if (edge >= 0) {
        return edge;
    }
    for (int i = 0; i < static_cast<int>(app.issues.size()); ++i) {
        if (app.issues[static_cast<size_t>(i)].begin > offset) {
            return i;
        }
    }
    return app.issues.empty() ? -1 : 0;
}

void refresh(App& app) {
    if (!app.analyze) {
        return;
    }
    app.issues = analyze_document(app.editor.text(), app.dictionary);
    if (app.composing) {
        const size_t offset = app.editor.cursor_offset();
        std::erase_if(app.issues, [&](const Issue& issue) {
            return issue.kind == IssueKind::Spelling && offset >= issue.begin && offset <= issue.end;
        });
    }
    app.analyze = false;
    app.panel_scroll = 0;
}

void ensure_suggestions(App& app, Issue& issue) {
    if (issue.kind != IssueKind::Spelling || issue.suggestions_ready) {
        return;
    }
    const std::string body = app.editor.text();
    if (issue.end > body.size() || issue.begin > issue.end) {
        issue.suggestions_ready = true;
        return;
    }
    issue.replacements = app.dictionary.suggest(body.substr(issue.begin, issue.end - issue.begin), 6, true);
    issue.suggestions_ready = true;
}

std::string display_name(const Editor& editor) {
    if (editor.path().empty()) {
        return "Untitled";
    }
    const std::filesystem::path path(editor.path());
    const std::string name = path.filename().string();
    return name.empty() ? editor.path() : name;
}

std::string reading_time(int words) {
    if (words <= 0) {
        return "0 min";
    }
    if (words < 200) {
        return "<1 min";
    }
    return std::to_string((words + 199) / 200) + " min";
}

void remember_column(App& app, const Metrics& metrics) {
    const auto rows = wrap_document(app.editor.lines(), metrics.text_width);
    const int index = row_for_cursor(rows, app.editor.lines(), app.editor.cursor_line(), app.editor.cursor_col());
    const VisualRow& row = rows[static_cast<size_t>(index)];
    const std::string& line = app.editor.lines()[static_cast<size_t>(row.line)];
    const int end = std::min(app.editor.cursor_col(), row.end);
    if (end <= row.begin) {
        app.preferred_col = 0;
        return;
    }
    app.preferred_col = string_width(std::string_view(line).substr(static_cast<size_t>(row.begin),
                                                                    static_cast<size_t>(end - row.begin)));
}

void move_visual(App& app, const Metrics& metrics, int delta) {
    const auto rows = wrap_document(app.editor.lines(), metrics.text_width);
    const int index = row_for_cursor(rows, app.editor.lines(), app.editor.cursor_line(), app.editor.cursor_col());
    const int next = std::clamp(index + delta, 0, static_cast<int>(rows.size()) - 1);
    const VisualRow& row = rows[static_cast<size_t>(next)];
    const std::string& line = app.editor.lines()[static_cast<size_t>(row.line)];
    app.editor.set_cursor(row.line, byte_for_display_col(line, row, app.preferred_col));
}

std::optional<WordSpan> word_at(const std::string& text, size_t offset) {
    const WordSpan* edge = nullptr;
    for (const WordSpan& word : scan_words(text)) {
        if (offset >= word.begin && offset < word.end) {
            return word;
        }
        if (offset == word.end) {
            edge = &word;
        }
    }
    if (edge != nullptr) {
        return *edge;
    }
    return std::nullopt;
}

bool blank_line(const std::string& line) {
    return std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

std::pair<size_t, size_t> paragraph_span(const Editor& editor) {
    const auto& lines = editor.lines();
    int index = editor.cursor_line();
    if (blank_line(lines[static_cast<size_t>(index)]) && index > 0 &&
        !blank_line(lines[static_cast<size_t>(index - 1)])) {
        --index;
    }
    int first = index;
    while (first > 0 && !blank_line(lines[static_cast<size_t>(first - 1)])) {
        --first;
    }
    int last = index;
    while (last + 1 < static_cast<int>(lines.size()) && !blank_line(lines[static_cast<size_t>(last + 1)])) {
        ++last;
    }
    return {editor.offset_of(first, 0), editor.offset_of(last, static_cast<int>(lines[static_cast<size_t>(last)].size()))};
}

void jump_issue(App& app, int direction) {
    if (app.issues.empty()) {
        app.status = "No notes on this page";
        return;
    }
    const int current = active_issue(app);
    const size_t origin = current >= 0 ? app.issues[static_cast<size_t>(current)].begin : app.editor.cursor_offset();
    int target = current;
    if (direction > 0) {
        target = 0;
        for (int i = 0; i < static_cast<int>(app.issues.size()); ++i) {
            if (app.issues[static_cast<size_t>(i)].begin > origin) {
                target = i;
                break;
            }
        }
    } else {
        target = static_cast<int>(app.issues.size()) - 1;
        for (int i = static_cast<int>(app.issues.size()) - 1; i >= 0; --i) {
            if (app.issues[static_cast<size_t>(i)].begin < origin) {
                target = i;
                break;
            }
        }
    }
    const auto [line, col] = app.editor.cursor_at(app.issues[static_cast<size_t>(target)].begin);
    app.editor.set_cursor(line, col);
}

void apply_issue(App& app) {
    const int index = active_issue(app);
    if (index < 0) {
        app.status = "No note under the cursor";
        return;
    }
    Issue& issue = app.issues[static_cast<size_t>(index)];
    ensure_suggestions(app, issue);
    if (issue.replacements.empty() && !(issue.suggestions_ready && issue.kind != IssueKind::Spelling)) {
        app.status = "No suggestion for this note";
        return;
    }
    if (issue.replacements.empty()) {
        app.status = "No suggestion for this note";
        return;
    }
    app.editor.replace_span(issue.begin, issue.end, issue.replacements.front());
    app.analyze = true;
    app.status = "Applied";
}

std::string save_document(App& app, const std::string& requested) {
    std::string path = requested.empty() ? app.editor.path() : requested;
    if (path.empty()) {
        return "Use save <path>";
    }
    std::string error;
    if (!app.editor.save_file(path, error)) {
        return error;
    }
    return "Saved " + display_name(app.editor);
}

std::string open_document(App& app, const std::string& path, bool force) {
    if (path.empty()) {
        return "Use open <path>";
    }
    if (app.editor.dirty() && !force) {
        return "Unsaved changes. Use open! " + path + " to discard them.";
    }
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return "That path is a directory";
    }
    if (!std::filesystem::exists(path, ec)) {
        app.editor = Editor{};
        app.editor.set_path(path);
        app.analyze = true;
        return "New file " + path;
    }
    Editor fresh;
    std::string error;
    if (!fresh.load_file(path, error)) {
        return error;
    }
    app.editor = std::move(fresh);
    app.scroll = 0;
    app.analyze = true;
    return "Opened " + display_name(app.editor);
}

void copy_selection(App& app, Terminal& terminal) {
    std::string text = app.editor.has_selection() ? app.editor.selection_text() : std::string{};
    if (text.empty()) {
        const auto [begin, end] = paragraph_span(app.editor);
        const std::string body = app.editor.text();
        text = body.substr(begin, end - begin);
    }
    if (text.empty()) {
        app.status = "Nothing to copy";
        return;
    }
    if (text.size() > 100000) {
        app.status = "The selection is too large to copy";
        return;
    }
    terminal.write("\033]52;c;" + base64_encode(text) + "\a");
    app.status = "Copied";
}

std::string word_under_cursor(const Editor& editor) {
    const std::string body = editor.text();
    const auto word = word_at(body, editor.cursor_offset());
    if (!word) {
        return {};
    }
    return body.substr(word->begin, word->end - word->begin);
}

void start_command(App& app, const std::string& seed) {
    app.mode = Mode::Command;
    app.command = seed;
    app.command_col = app.command.size();
}

std::string run_command(App& app, Terminal& terminal, std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    size_t start = 0;
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])) != 0) {
        ++start;
    }
    line = line.substr(start);
    if (line.empty()) {
        return {};
    }
    app.last_command = line;
    std::string name = line;
    std::string rest;
    const size_t space = line.find(' ');
    if (space != std::string::npos) {
        name = line.substr(0, space);
        rest = line.substr(space + 1);
        while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())) != 0) {
            rest.erase(rest.begin());
        }
    }
    bool force = false;
    if (!name.empty() && name.back() == '!') {
        force = true;
        name.pop_back();
    }
    name = ascii_lower(name);
    if (name == "quit") {
        if (app.editor.dirty() && !force) {
            return "Unsaved changes. Use quit! to leave anyway.";
        }
        app.running = false;
        return {};
    }
    if (name == "help") {
        app.mode = Mode::Help;
        app.help_scroll = 0;
        return {};
    }
    if (name == "save") {
        return save_document(app, rest);
    }
    if (name == "open") {
        return open_document(app, rest, force);
    }
    if (name == "learn") {
        const std::string word = word_under_cursor(app.editor);
        if (word.empty()) {
            return "Put the cursor on a word first";
        }
        std::string error;
        if (!app.dictionary.learn(word, error)) {
            return error;
        }
        app.analyze = true;
        return "Remembered \"" + word + "\"";
    }
    if (name == "ignore") {
        const std::string word = word_under_cursor(app.editor);
        if (word.empty()) {
            return "Put the cursor on a word first";
        }
        app.dictionary.ignore(word);
        app.analyze = true;
        return "Ignoring \"" + word + "\" for this session";
    }
    if (name == "copy") {
        copy_selection(app, terminal);
        return app.status;
    }
    return "Unknown command. Try help.";
}

std::string render(App& app, const Metrics& metrics);

void move_to(std::string& out, int row, int col) {
    out += "\033[";
    out += std::to_string(row);
    out += ';';
    out += std::to_string(col);
    out += 'H';
}

std::vector<std::string> wrap_block(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }
    lines.push_back(current);
    if (lines.empty()) {
        lines.emplace_back();
    }
    const auto rows = wrap_document(lines, std::max(1, width));
    std::vector<std::string> visual;
    visual.reserve(rows.size());
    for (const VisualRow& row : rows) {
        const std::string& source = lines[static_cast<size_t>(row.line)];
        if (row.begin >= static_cast<int>(source.size()) || row.end <= row.begin) {
            visual.emplace_back();
        } else {
            visual.push_back(source.substr(static_cast<size_t>(row.begin), static_cast<size_t>(row.end - row.begin)));
        }
    }
    return visual;
}

std::string issue_line(const App& app, const Issue& issue, bool active, int width) {
    const std::string body = app.editor.text();
    int line = 1;
    int column = 1;
    locate_offset(body, issue.begin, line, column);
    std::string text = active ? "> " : "  ";
    text += std::to_string(line);
    text += "  ";
    text += issue_kind_name(issue.kind);
    text += "  ";
    text += snippet(body, issue.begin, issue.end, 28);
    if (!issue.replacements.empty()) {
        text += "  →  ";
        for (size_t i = 0; i < issue.replacements.size() && i < 3; ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += issue.replacements[i].empty() ? std::string("delete") : issue.replacements[i];
        }
    } else if (!issue.message.empty()) {
        text += "  ";
        text += issue.message;
    }
    return clip_to_width(text, width);
}

std::string render(App& app, const Metrics& metrics) {
    refresh(app);
    const auto rows = wrap_document(app.editor.lines(), metrics.text_width);
    const int cursor_row = row_for_cursor(rows, app.editor.lines(), app.editor.cursor_line(), app.editor.cursor_col());
    if (cursor_row < app.scroll) {
        app.scroll = cursor_row;
    }
    if (cursor_row >= app.scroll + metrics.editor_rows) {
        app.scroll = cursor_row - metrics.editor_rows + 1;
    }
    if (app.scroll < 0) {
        app.scroll = 0;
    }

    const std::string body = app.editor.text();
    const auto starts = line_offsets(app.editor.lines());
    std::optional<std::pair<size_t, size_t>> selection;
    if (app.editor.has_selection()) {
        selection = app.editor.selection_bounds();
    }

    const int focus = active_issue(app);
    if (app.mode == Mode::Edit && focus >= 0 && metrics.body_rows > 0) {
        if (focus < app.panel_scroll) {
            app.panel_scroll = focus;
        }
        if (focus >= app.panel_scroll + metrics.body_rows) {
            app.panel_scroll = focus - metrics.body_rows + 1;
        }
        const int last = std::min(static_cast<int>(app.issues.size()), app.panel_scroll + metrics.body_rows);
        for (int i = app.panel_scroll; i < last; ++i) {
            ensure_suggestions(app, app.issues[static_cast<size_t>(i)]);
        }
    }

    std::string out;
    out.reserve(static_cast<size_t>(metrics.rows * metrics.cols));
    out += "\033[?25l";

    for (int screen = 0; screen < metrics.editor_rows; ++screen) {
        move_to(out, screen + 1, 1);
        const int visual = app.scroll + screen;
        if (visual < 0 || visual >= static_cast<int>(rows.size())) {
            out += "\033[0m\033[K";
            continue;
        }
        const VisualRow& row = rows[static_cast<size_t>(visual)];
        const std::string& line = app.editor.lines()[static_cast<size_t>(row.line)];
        const bool current = row.line == app.editor.cursor_line();
        if (metrics.gutter > 0) {
            std::string number = std::to_string(row.line + 1);
            if (static_cast<int>(number.size()) < metrics.gutter - 1) {
                number = std::string(static_cast<size_t>(metrics.gutter - 1 - static_cast<int>(number.size())), ' ') + number;
            }
            number.push_back(' ');
            number = clip_to_width(number, metrics.gutter);
            out += current ? "\033[0;33m" : "\033[0;2m";
            out += number;
            if (string_width(number) < metrics.gutter) {
                out += std::string(static_cast<size_t>(metrics.gutter - string_width(number)), ' ');
            }
            out += "\033[0m";
        }

        std::vector<unsigned char> marks(line.size(), 0);
        const size_t origin = starts[static_cast<size_t>(row.line)];
        for (const Issue& issue : app.issues) {
            if (issue.end <= origin || issue.begin >= origin + line.size()) {
                continue;
            }
            const size_t from = issue.begin > origin ? issue.begin - origin : 0;
            const size_t to = std::min(line.size(), issue.end > origin ? issue.end - origin : 0);
            unsigned char kind = 2;
            if (issue.kind == IssueKind::Spelling) {
                kind = 1;
            } else if (issue.kind == IssueKind::Style || issue.kind == IssueKind::Name) {
                kind = 3;
            }
            for (size_t i = from; i < to && i < marks.size(); ++i) {
                marks[i] = kind;
            }
        }

        int column = 0;
        size_t index = static_cast<size_t>(std::max(0, row.begin));
        const size_t stop = std::min(line.size(), static_cast<size_t>(std::max(row.begin, row.end)));
        while (index < stop && column < metrics.text_width) {
            const size_t absolute = origin + index;
            const bool selected = selection && absolute >= selection->first && absolute < selection->second;
            const unsigned char mark = index < marks.size() ? marks[index] : 0;
            size_t next = index;
            int next_col = column;
            while (next < stop) {
                const bool next_selected = selection && (origin + next) >= selection->first && (origin + next) < selection->second;
                const unsigned char next_mark = next < marks.size() ? marks[next] : 0;
                if (next_selected != selected || next_mark != mark) {
                    break;
                }
                int length = 1;
                const char32_t cp = utf8_decode(line, next, length);
                if (length < 1) {
                    length = 1;
                }
                int width = 1;
                if (cp == '\t') {
                    width = 4 - (next_col % 4);
                } else {
                    const int measured = codepoint_width(cp);
                    width = measured > 0 ? measured : (cp < 32 ? 0 : 1);
                }
                if (next_col + width > metrics.text_width && next_col > column) {
                    break;
                }
                if (next_col + width > metrics.text_width && width > 0) {
                    break;
                }
                next_col += width;
                next += static_cast<size_t>(length);
            }
            if (next == index) {
                break;
            }
            out += "\033[0m";
            if (selected) {
                out += "\033[7m";
            }
            if (mark == 1) {
                out += "\033[4;31m";
            } else if (mark == 2) {
                out += "\033[4;33m";
            } else if (mark == 3) {
                out += "\033[4;36m";
            }
            out.append(line, index, next - index);
            column = next_col;
            index = next;
        }
        out += "\033[0m\033[K";
    }

    move_to(out, metrics.panel_top, 1);
    std::string title = "Notes";
    if (!app.issues.empty()) {
        int spelling = 0;
        int grammar = 0;
        int style = 0;
        for (const Issue& issue : app.issues) {
            if (issue.kind == IssueKind::Spelling) {
                ++spelling;
            } else if (issue.kind == IssueKind::Grammar) {
                ++grammar;
            } else {
                ++style;
            }
        }
        title = "Notes  " + std::to_string(spelling) + " spelling · " + std::to_string(grammar) + " grammar";
        if (style > 0) {
            title += " · " + std::to_string(style) + " style";
        }
    } else if (app.mode == Mode::Command) {
        title = "Command";
    } else if (app.mode == Mode::Preview) {
        title = "Revision";
    } else if (app.mode == Mode::Help) {
        title = "Help";
    }
    std::string border = "─ " + title + " ";
    while (string_width(border) < metrics.cols) {
        border += "─";
    }
    border = clip_to_width(border, metrics.cols);
    out += "\033[0;33m";
    out += border;
    out += "\033[0m";

    std::vector<std::string> panel_lines;
    if (app.mode == Mode::Help) {
        static const char* help =
            "Write in the page above. Problems are underlined as you go: red for spelling, gold for grammar, "
            "blue for style.\n\n"
            "F1 help    F2 next note    F3 previous    F4 apply suggestion\n"
            "Ctrl-P command    Ctrl-Space select    Ctrl-S save    Ctrl-O open\n"
            "Ctrl-Z undo    Ctrl-Y redo    Ctrl-Q quit    arrows move    Ctrl-arrows move by word\n\n"
            "Commands: learn, ignore, save, open, copy, quit";
        panel_lines = wrap_block(help, metrics.cols);
    } else if (app.mode == Mode::Preview) {
        panel_lines = wrap_block(app.preview, metrics.cols);
    } else if (app.mode == Mode::Command) {
        const std::string typed = ascii_lower(app.command);
        const std::string first = typed.substr(0, typed.find(' '));
        for (const CommandSpec& spec : kCommands) {
            if (first.empty() || std::string(spec.name).rfind(first, 0) == 0) {
                panel_lines.push_back(std::string(spec.name) + "   " + spec.help);
            }
        }
    } else if (app.issues.empty()) {
        std::string empty = "No spelling or grammar notes.\nUnderlines show up as you write.";
        if (!app.dictionary.ready()) {
            empty += "\n" + (app.dict_error.empty() ? std::string("The dictionary did not load.") : app.dict_error);
        }
        panel_lines = wrap_block(empty, metrics.cols);
    } else {
        const int last = std::min(static_cast<int>(app.issues.size()), app.panel_scroll + std::max(1, metrics.body_rows));
        for (int i = app.panel_scroll; i < last; ++i) {
            panel_lines.push_back(issue_line(app, app.issues[static_cast<size_t>(i)], i == focus, metrics.cols));
        }
    }

    int panel_offset = 0;
    if (app.mode == Mode::Preview) {
        panel_offset = app.preview_scroll;
    } else if (app.mode == Mode::Help) {
        panel_offset = app.help_scroll;
    }
    if (panel_offset < 0) {
        panel_offset = 0;
    }
    for (int i = 0; i < metrics.body_rows; ++i) {
        move_to(out, metrics.panel_top + 1 + i, 1);
        const int source = panel_offset + i;
        if (source >= 0 && source < static_cast<int>(panel_lines.size())) {
            out += "\033[0m";
            out += clip_to_width(panel_lines[static_cast<size_t>(source)], metrics.cols);
        }
        out += "\033[0m\033[K";
    }

    move_to(out, metrics.hint_row, 1);
    std::string hint = "F1 help   F2 next   F4 apply   ^P command   ^S save";
    if (app.mode == Mode::Command) {
        hint = "> " + app.command;
    } else if (app.mode == Mode::Preview) {
        hint = "Enter accepts the revision    Esc discards it";
    } else if (app.mode == Mode::Help) {
        hint = "Esc closes help";
    }
    out += "\033[0;2m";
    out += clip_to_width(hint, metrics.cols);
    out += "\033[0m\033[K";

    int spelling = 0;
    int grammar = 0;
    for (const Issue& issue : app.issues) {
        if (issue.kind == IssueKind::Spelling) {
            ++spelling;
        } else if (issue.kind == IssueKind::Grammar) {
            ++grammar;
        }
    }
    const int words = static_cast<int>(scan_words(body).size());
    int code_column = 1;
    if (app.editor.cursor_col() > 0) {
        code_column = 1 + utf8_count(app.editor.lines()[static_cast<size_t>(app.editor.cursor_line())].substr(
                           0, static_cast<size_t>(app.editor.cursor_col())));
    }
    std::string left = " Inkwell  " + display_name(app.editor);
    if (app.editor.dirty()) {
        left += " *";
    }
    std::string right;
    if (!app.status.empty()) {
        right = app.status;
    } else {
        right = std::to_string(words) + " words  ·  " + reading_time(words) + "  ·  " + std::to_string(spelling) +
                " spelling  " + std::to_string(grammar) + " grammar  ·  Ln " +
                std::to_string(app.editor.cursor_line() + 1) + ", Col " + std::to_string(code_column);
        if (app.editor.mark_active()) {
            right += "  ·  selecting";
        }
    }
    right = " " + right + " ";
    std::string bar;
    const int room = metrics.cols - 1 - string_width(left) - string_width(right);
    if (room >= 1) {
        bar = left + std::string(static_cast<size_t>(room), ' ') + right;
    } else {
        bar = clip_to_width(left + "  " + right, metrics.cols - 1);
    }
    move_to(out, metrics.rows, 1);
    out += "\033[7m";
    out += bar;
    out += "\033[K\033[0m";

    int cursor_screen_row = cursor_row - app.scroll + 1;
    int cursor_screen_col = 1;
    if (app.mode == Mode::Command) {
        cursor_screen_row = metrics.hint_row;
        const std::string before = app.command.substr(0, std::min(app.command_col, app.command.size()));
        cursor_screen_col = 3 + string_width(before);
    } else if (cursor_screen_row >= 1 && cursor_screen_row <= metrics.editor_rows &&
               cursor_row >= 0 && cursor_row < static_cast<int>(rows.size())) {
        const VisualRow& row = rows[static_cast<size_t>(cursor_row)];
        const std::string& line = app.editor.lines()[static_cast<size_t>(row.line)];
        const int end = std::min(app.editor.cursor_col(), std::max(row.begin, row.end));
        int cells = 0;
        if (end > row.begin) {
            cells = string_width(std::string_view(line).substr(static_cast<size_t>(row.begin),
                                                                static_cast<size_t>(end - row.begin)));
        }
        cursor_screen_col = metrics.gutter + cells + 1;
    }
    if (cursor_screen_col < 1) {
        cursor_screen_col = 1;
    }
    if (cursor_screen_col > metrics.cols) {
        cursor_screen_col = metrics.cols;
    }
    move_to(out, std::clamp(cursor_screen_row, 1, metrics.rows), cursor_screen_col);
    out += "\033[?25h";
    return out;
}

void insert_command(App& app, std::string text) {
    app.command.insert(app.command_col, text);
    app.command_col += text.size();
}

void complete_command(App& app) {
    const size_t space = app.command.find(' ');
    if (space != std::string::npos) {
        return;
    }
    const std::string prefix = ascii_lower(app.command);
    std::vector<std::string> matches;
    for (const CommandSpec& spec : kCommands) {
        const std::string name = spec.name;
        if (name.rfind(prefix, 0) == 0) {
            matches.push_back(name);
        }
    }
    if (matches.size() == 1) {
        app.command = matches.front() + " ";
        app.command_col = app.command.size();
        return;
    }
    if (matches.empty()) {
        return;
    }
    std::string shared = matches.front();
    for (const std::string& match : matches) {
        size_t i = 0;
        while (i < shared.size() && i < match.size() && shared[i] == match[i]) {
            ++i;
        }
        shared.resize(i);
    }
    if (shared.size() > prefix.size()) {
        app.command = shared;
        app.command_col = app.command.size();
    }
}

void handle_editor_key(App& app, const Metrics& metrics, const Key& key) {
    switch (key.kind) {
        case KeyKind::Rune: {
            std::string text;
            append_utf8(text, key.rune);
            app.editor.insert(std::move(text));
            app.analyze = true;
            remember_column(app, metrics);
            break;
        }
        case KeyKind::Paste:
            app.editor.insert(key.text);
            app.analyze = true;
            remember_column(app, metrics);
            break;
        case KeyKind::Enter:
            app.editor.newline();
            app.analyze = true;
            remember_column(app, metrics);
            break;
        case KeyKind::Tab:
            app.editor.insert("  ");
            app.analyze = true;
            remember_column(app, metrics);
            break;
        case KeyKind::Backspace:
            app.editor.backspace();
            app.analyze = true;
            remember_column(app, metrics);
            break;
        case KeyKind::Delete:
            app.editor.delete_forward();
            app.analyze = true;
            remember_column(app, metrics);
            break;
        case KeyKind::Left:
            if (key.ctrl) {
                app.editor.move_word_left();
            } else {
                app.editor.move_left();
            }
            remember_column(app, metrics);
            break;
        case KeyKind::Right:
            if (key.ctrl) {
                app.editor.move_word_right();
            } else {
                app.editor.move_right();
            }
            remember_column(app, metrics);
            break;
        case KeyKind::Up:
            move_visual(app, metrics, -1);
            break;
        case KeyKind::Down:
            move_visual(app, metrics, 1);
            break;
        case KeyKind::PageUp:
            move_visual(app, metrics, -(metrics.editor_rows - 1));
            break;
        case KeyKind::PageDown:
            move_visual(app, metrics, metrics.editor_rows - 1);
            break;
        case KeyKind::Home:
            if (key.ctrl) {
                app.editor.move_document_start();
            } else {
                app.editor.move_home();
            }
            remember_column(app, metrics);
            break;
        case KeyKind::End:
            if (key.ctrl) {
                app.editor.move_document_end();
            } else {
                app.editor.move_end();
            }
            remember_column(app, metrics);
            break;
        case KeyKind::Mark:
            app.editor.toggle_mark();
            break;
        case KeyKind::Function:
            if (key.code == 1) {
                app.mode = Mode::Help;
                app.help_scroll = 0;
            } else if (key.code == 2) {
                jump_issue(app, 1);
                remember_column(app, metrics);
            } else if (key.code == 3) {
                jump_issue(app, -1);
                remember_column(app, metrics);
            } else if (key.code == 4) {
                apply_issue(app);
                remember_column(app, metrics);
            }
            break;
        case KeyKind::Ctrl:
            if (key.code == 'a') {
                app.editor.move_home();
                remember_column(app, metrics);
            } else if (key.code == 'e') {
                app.editor.move_end();
                remember_column(app, metrics);
            } else if (key.code == 's') {
                app.status = save_document(app, {});
                if (app.status.rfind("Use save", 0) == 0) {
                    start_command(app, "save ");
                    app.status = "Name the file";
                }
            } else if (key.code == 'o') {
                start_command(app, "open ");
            } else if (key.code == 'p') {
                start_command(app, {});
            } else if (key.code == 'z') {
                app.editor.undo();
                app.analyze = true;
                remember_column(app, metrics);
            } else if (key.code == 'y') {
                app.editor.redo();
                app.analyze = true;
                remember_column(app, metrics);
            } else if (key.code == 'q') {
                if (!app.editor.dirty() || app.quit_armed) {
                    app.running = false;
                } else {
                    app.quit_armed = true;
                    app.status = "Unsaved changes. Ctrl-Q again to quit.";
                }
            }
            break;
        default:
            break;
    }
}

void handle_command_key(App& app, Terminal& terminal, const Key& key) {
    if (key.kind == KeyKind::Escape) {
        app.mode = Mode::Edit;
        return;
    }
    if (key.kind == KeyKind::Enter) {
        const std::string line = app.command;
        app.command.clear();
        app.command_col = 0;
        const std::string message = run_command(app, terminal, line);
        if (app.mode == Mode::Command) {
            app.mode = Mode::Edit;
        }
        if (!message.empty()) {
            app.status = message;
        }
        return;
    }
    if (key.kind == KeyKind::Backspace) {
        if (app.command_col > 0) {
            const size_t prev = utf8_prev(app.command, app.command_col);
            app.command.erase(prev, app.command_col - prev);
            app.command_col = prev;
        }
        return;
    }
    if (key.kind == KeyKind::Left) {
        app.command_col = utf8_prev(app.command, app.command_col);
        return;
    }
    if (key.kind == KeyKind::Right) {
        if (app.command_col < app.command.size()) {
            int length = 1;
            utf8_decode(app.command, app.command_col, length);
            app.command_col += static_cast<size_t>(std::max(1, length));
        }
        return;
    }
    if (key.kind == KeyKind::Home || (key.kind == KeyKind::Ctrl && key.code == 'a')) {
        app.command_col = 0;
        return;
    }
    if (key.kind == KeyKind::End || (key.kind == KeyKind::Ctrl && key.code == 'e')) {
        app.command_col = app.command.size();
        return;
    }
    if (key.kind == KeyKind::Up) {
        app.command = app.last_command;
        app.command_col = app.command.size();
        return;
    }
    if (key.kind == KeyKind::Tab) {
        complete_command(app);
        return;
    }
    if (key.kind == KeyKind::Rune) {
        std::string text;
        append_utf8(text, key.rune);
        insert_command(app, std::move(text));
        return;
    }
    if (key.kind == KeyKind::Paste) {
        std::string flat = key.text;
        for (char& c : flat) {
            if (c == '\n' || c == '\r') {
                c = ' ';
            }
        }
        insert_command(app, std::move(flat));
    }
}

}  // namespace

int run_app(const std::string& path) {
    std::setlocale(LC_ALL, "");
    App app;
    if (!app.dictionary.load(app.dict_error)) {
        app.dict_error = app.dict_error.empty() ? "The dictionary did not load." : app.dict_error;
    }
    if (!path.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            app.status = "That path is a directory";
        } else if (std::filesystem::exists(path, ec)) {
            std::string error;
            if (!app.editor.load_file(path, error)) {
                app.status = error;
            }
        } else {
            app.editor.set_path(path);
            app.status = "New file";
        }
    }

    for (const std::string& plugin_error : plugin_errors()) {
        std::cerr << plugin_error << '\n';
    }

    Terminal terminal;
    std::string error;
    if (!terminal.enter(error)) {
        std::cerr << error << '\n';
        return 2;
    }

    while (app.running) {
        terminal.update_size();
        const Metrics metrics = measure(terminal.cols, terminal.rows, static_cast<int>(app.editor.lines().size()));
        terminal.write(render(app, metrics));
        const Key key = terminal.read_key(app.composing ? 400 : -1);
        if (key.kind == KeyKind::Eof) {
            break;
        }
        if (key.kind == KeyKind::Resize || key.kind == KeyKind::None) {
            if (key.kind == KeyKind::None) {
                app.composing = false;
                app.analyze = true;
            }
            continue;
        }
        if (!(key.kind == KeyKind::Ctrl && key.code == 'q')) {
            app.quit_armed = false;
        }
        app.status.clear();

        if (key.kind == KeyKind::Ctrl && key.code == 'q' && app.mode != Mode::Edit) {
            if (!app.editor.dirty() || app.quit_armed) {
                break;
            }
            app.quit_armed = true;
            app.mode = Mode::Edit;
            app.status = "Unsaved changes. Ctrl-Q again to quit.";
            continue;
        }
        if (app.mode == Mode::Help) {
            if (key.kind == KeyKind::PageUp) {
                app.help_scroll = std::max(0, app.help_scroll - std::max(1, metrics.body_rows - 1));
            } else if (key.kind == KeyKind::PageDown) {
                app.help_scroll += std::max(1, metrics.body_rows - 1);
            } else if (key.kind == KeyKind::Up) {
                app.help_scroll = std::max(0, app.help_scroll - 1);
            } else if (key.kind == KeyKind::Down) {
                ++app.help_scroll;
            } else {
                app.mode = Mode::Edit;
            }
            continue;
        }
        if (app.mode == Mode::Preview) {
            if (key.kind == KeyKind::Enter) {
                app.editor.replace_span(app.preview_begin, app.preview_end, app.preview);
                app.analyze = true;
                app.mode = Mode::Edit;
                app.status = "Revision applied";
                remember_column(app, metrics);
            } else if (key.kind == KeyKind::Escape) {
                app.mode = Mode::Edit;
                app.status = "Discarded the revision";
            } else if (key.kind == KeyKind::PageUp || key.kind == KeyKind::Up) {
                app.preview_scroll = std::max(0, app.preview_scroll - (key.kind == KeyKind::Up ? 1 : metrics.body_rows));
            } else if (key.kind == KeyKind::PageDown || key.kind == KeyKind::Down) {
                app.preview_scroll += key.kind == KeyKind::Down ? 1 : metrics.body_rows;
            } else {
                app.status = "Enter accepts the revision. Esc discards it.";
            }
            continue;
        }
        if (app.mode == Mode::Command) {
            handle_command_key(app, terminal, key);
            continue;
        }
        const bool keeps_composing = key.kind == KeyKind::Rune || key.kind == KeyKind::Paste || key.kind == KeyKind::Backspace ||
                                     key.kind == KeyKind::Delete || key.kind == KeyKind::Tab;
        if (app.composing && !keeps_composing) {
            app.analyze = true;
        }
        app.composing = keeps_composing;
        handle_editor_key(app, metrics, key);
    }
    terminal.leave();
    return 0;
}

}  // namespace inkwell
