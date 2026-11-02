#pragma once

#include <QString>

#include <string>
#include <vector>

namespace inkwell {

struct TextMap {
    QString text;
    std::string utf8;
    std::vector<int> byte_to_q;
    std::vector<int> q_to_byte;
};

inline int utf8_width(char32_t codepoint) {
    if (codepoint < 0x80) {
        return 1;
    }
    if (codepoint < 0x800) {
        return 2;
    }
    if (codepoint < 0x10000) {
        return 3;
    }
    return 4;
}

inline TextMap map_text(const QString& text) {
    TextMap mapped;
    mapped.text = text;
    const QByteArray bytes = text.toUtf8();
    mapped.utf8.assign(bytes.constData(), static_cast<size_t>(bytes.size()));
    mapped.byte_to_q.assign(mapped.utf8.size() + 1, text.size());
    mapped.q_to_byte.assign(static_cast<size_t>(text.size()) + 1, static_cast<int>(mapped.utf8.size()));

    qsizetype qi = 0;
    int bi = 0;
    while (qi < text.size() && bi < static_cast<int>(mapped.utf8.size())) {
        mapped.q_to_byte[static_cast<size_t>(qi)] = bi;
        mapped.byte_to_q[static_cast<size_t>(bi)] = qi;
        char32_t codepoint = text[qi].unicode();
        int qstep = 1;
        if (text[qi].isHighSurrogate() && qi + 1 < text.size() && text[qi + 1].isLowSurrogate()) {
            codepoint = QChar::surrogateToUcs4(text[qi], text[qi + 1]);
            qstep = 2;
            mapped.q_to_byte[static_cast<size_t>(qi + 1)] = bi;
        }
        const int bstep = utf8_width(codepoint);
        for (int step = 1; step < bstep && bi + step <= static_cast<int>(mapped.utf8.size()); ++step) {
            mapped.byte_to_q[static_cast<size_t>(bi + step)] = qi;
        }
        qi += qstep;
        bi += bstep;
    }
    mapped.q_to_byte[static_cast<size_t>(text.size())] = static_cast<int>(mapped.utf8.size());
    mapped.byte_to_q[mapped.utf8.size()] = text.size();
    return mapped;
}

inline int q_at(const TextMap& mapped, size_t byte) {
    if (mapped.byte_to_q.empty()) {
        return 0;
    }
    if (byte >= mapped.byte_to_q.size()) {
        return mapped.text.size();
    }
    return mapped.byte_to_q[byte];
}

inline size_t byte_at(const TextMap& mapped, int index) {
    if (mapped.q_to_byte.empty()) {
        return 0;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= static_cast<int>(mapped.q_to_byte.size())) {
        return mapped.utf8.size();
    }
    return static_cast<size_t>(mapped.q_to_byte[static_cast<size_t>(index)]);
}

}  // namespace inkwell
