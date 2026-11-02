#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace inkwell {

struct WordSpan {
    size_t begin = 0;
    size_t end = 0;
};

std::vector<WordSpan> scan_words(std::string_view text);
std::optional<WordSpan> word_touching(std::string_view text, size_t offset);
std::string snippet(std::string_view text, size_t begin, size_t end, int max_chars);

void locate_offset(std::string_view text, size_t offset, int& line, int& column);

}  // namespace inkwell
