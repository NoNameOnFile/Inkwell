#include "text.hpp"

#include "utf8.hpp"

#include <cctype>

namespace inkwell {
namespace {

bool is_word_byte(unsigned char c) {
    if (c >= 128) {
        return true;
    }
    if (std::isalnum(c) != 0) {
        return true;
    }
    return c == '\'' || c == '-';
}

}  // namespace

std::vector<WordSpan> scan_words(std::string_view text) {
    std::vector<WordSpan> words;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        if (!is_word_byte(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < n && is_word_byte(static_cast<unsigned char>(text[j]))) {
            ++j;
        }
        size_t begin = i;
        size_t end = j;
        while (begin < end && text[begin] == '-') {
            ++begin;
        }
        while (end > begin && text[end - 1] == '-') {
            --end;
        }
        if (end - begin >= 2 && text[begin] == '\'' && text[end - 1] == '\'') {
            ++begin;
            --end;
        } else if (end > begin && text[end - 1] == '\'') {
            --end;
        }
        if (begin < end) {
            words.push_back(WordSpan{begin, end});
        }
        i = j;
    }
    return words;
}

std::string snippet(std::string_view text, size_t begin, size_t end, int max_chars) {
    if (begin > text.size()) {
        begin = text.size();
    }
    if (end > text.size()) {
        end = text.size();
    }
    if (end < begin) {
        end = begin;
    }
    std::string out;
    out.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\t' || c == '\r') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    if (max_chars > 0 && utf8_count(out) > max_chars) {
        out = clip_to_width(out, max_chars);
        out += "…";
    }
    return out;
}

void locate_offset(std::string_view text, size_t offset, int& line, int& column) {
    if (offset > text.size()) {
        offset = text.size();
    }
    line = 1;
    size_t line_start = 0;
    for (size_t i = 0; i < offset; ++i) {
        if (text[i] == '\n') {
            ++line;
            line_start = i + 1;
        }
    }
    column = 1 + utf8_count(text.substr(line_start, offset - line_start));
}

std::optional<WordSpan> word_touching(std::string_view text, size_t offset) {
    std::optional<WordSpan> edge;
    for (const WordSpan& word : scan_words(text)) {
        if (offset >= word.begin && offset < word.end) {
            return word;
        }
        if (offset == word.end) {
            edge = word;
        }
    }
    return edge;
}

}  // namespace inkwell
