#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "utf8.hpp"

#include <cwchar>
#include <string>

namespace inkwell {

int utf8_seq_len(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

char32_t utf8_decode(std::string_view text, size_t index, int& length) {
    if (index >= text.size()) {
        length = 0;
        return 0;
    }
    const auto lead = static_cast<unsigned char>(text[index]);
    length = utf8_seq_len(lead);
    if (index + static_cast<size_t>(length) > text.size()) {
        length = 1;
        return lead;
    }
    if (length == 1) {
        return lead;
    }
    char32_t code = 0;
    int bits = 0;
    if (length == 2) {
        code = lead & 0x1F;
        bits = 5;
    } else if (length == 3) {
        code = lead & 0x0F;
        bits = 4;
    } else {
        code = lead & 0x07;
        bits = 3;
    }
    for (int i = 1; i < length; ++i) {
        const auto cont = static_cast<unsigned char>(text[index + static_cast<size_t>(i)]);
        if ((cont & 0xC0) != 0x80) {
            length = 1;
            return lead;
        }
        code = (code << 6) | (cont & 0x3F);
        bits += 6;
    }
    (void)bits;
    return code;
}

size_t utf8_prev(std::string_view text, size_t index) {
    if (index == 0) {
        return 0;
    }
    --index;
    while (index > 0) {
        const auto c = static_cast<unsigned char>(text[index]);
        if ((c & 0xC0) != 0x80) {
            break;
        }
        --index;
    }
    return index;
}

void append_utf8(std::string& out, char32_t codepoint) {
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        out.push_back('?');
        return;
    }
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

int codepoint_width(char32_t codepoint) {
    if (codepoint == 0 || codepoint == '\n' || codepoint == '\r') {
        return 0;
    }
    if (codepoint < 32 || (codepoint >= 0x7F && codepoint < 0xA0)) {
        return 0;
    }
    const int width = ::wcwidth(static_cast<wchar_t>(codepoint));
    if (width < 0) {
        return 1;
    }
    return width;
}

int string_width(std::string_view text) {
    int column = 0;
    for (size_t i = 0; i < text.size();) {
        int length = 1;
        const char32_t cp = utf8_decode(text, i, length);
        if (length <= 0) {
            break;
        }
        if (cp == '\t') {
            column += 4 - (column % 4);
        } else {
            const int width = codepoint_width(cp);
            column += width > 0 ? width : (cp < 32 ? 0 : 1);
        }
        i += static_cast<size_t>(length);
    }
    return column;
}

int offset_for_width(std::string_view text, int target) {
    if (target <= 0) {
        return 0;
    }
    int column = 0;
    size_t i = 0;
    while (i < text.size()) {
        int length = 1;
        const char32_t cp = utf8_decode(text, i, length);
        if (length <= 0) {
            break;
        }
        int width = 1;
        if (cp == '\t') {
            width = 4 - (column % 4);
        } else {
            const int measured = codepoint_width(cp);
            width = measured > 0 ? measured : (cp < 32 ? 0 : 1);
        }
        if (column + width > target) {
            break;
        }
        column += width;
        i += static_cast<size_t>(length);
    }
    return static_cast<int>(i);
}

std::string clip_to_width(std::string_view text, int width) {
    if (width <= 0) {
        return {};
    }
    const int offset = offset_for_width(text, width);
    return std::string(text.substr(0, static_cast<size_t>(offset)));
}

int utf8_count(std::string_view text) {
    int count = 0;
    for (size_t i = 0; i < text.size();) {
        int length = 1;
        utf8_decode(text, i, length);
        if (length <= 0) {
            break;
        }
        ++count;
        i += static_cast<size_t>(length);
    }
    return count;
}

std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string ascii_upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

bool is_ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

}  // namespace inkwell
