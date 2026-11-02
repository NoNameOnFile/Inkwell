#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace inkwell {

int utf8_seq_len(unsigned char lead);
char32_t utf8_decode(std::string_view text, size_t index, int& length);
size_t utf8_prev(std::string_view text, size_t index);
void append_utf8(std::string& out, char32_t codepoint);

int codepoint_width(char32_t codepoint);
int string_width(std::string_view text);
int offset_for_width(std::string_view text, int target);
std::string clip_to_width(std::string_view text, int width);
int utf8_count(std::string_view text);

std::string ascii_lower(std::string_view text);
std::string ascii_upper(std::string_view text);
bool is_ascii_space(unsigned char c);

}  // namespace inkwell
