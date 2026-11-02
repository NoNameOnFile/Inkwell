#include "layout.hpp"

#include "utf8.hpp"

#include <algorithm>

namespace inkwell {

std::vector<VisualRow> wrap_document(const std::vector<std::string>& lines, int width) {
    if (width < 1) {
        width = 1;
    }
    std::vector<VisualRow> rows;
    for (int line_index = 0; line_index < static_cast<int>(lines.size()); ++line_index) {
        const std::string& line = lines[static_cast<size_t>(line_index)];
        if (line.empty()) {
            rows.push_back(VisualRow{line_index, 0, 0});
            continue;
        }
        int index = 0;
        const int length = static_cast<int>(line.size());
        while (index < length) {
            int cursor = index;
            int column = 0;
            int break_at = -1;
            while (cursor < length) {
                int seq = 1;
                const char32_t cp = utf8_decode(line, static_cast<size_t>(cursor), seq);
                if (seq < 1) {
                    seq = 1;
                }
                int cell = 1;
                if (cp == '\t') {
                    cell = 4 - (column % 4);
                } else {
                    const int measured = codepoint_width(cp);
                    cell = measured > 0 ? measured : (cp < 32 ? 0 : 1);
                }
                if (column > 0 && column + cell > width) {
                    break;
                }
                if (cp == ' ') {
                    break_at = cursor;
                }
                column += cell;
                cursor += seq;
            }
            if (cursor >= length) {
                rows.push_back(VisualRow{line_index, index, length});
                break;
            }
            if (break_at > index) {
                rows.push_back(VisualRow{line_index, index, break_at});
                index = break_at + 1;
            } else {
                if (cursor == index) {
                    int seq = 1;
                    utf8_decode(line, static_cast<size_t>(index), seq);
                    cursor = index + (seq > 0 ? seq : 1);
                }
                rows.push_back(VisualRow{line_index, index, cursor});
                index = cursor;
            }
        }
    }
    if (rows.empty()) {
        rows.push_back(VisualRow{0, 0, 0});
    }
    return rows;
}

int row_for_cursor(const std::vector<VisualRow>& rows, const std::vector<std::string>& lines, int line, int col) {
    int last = 0;
    const int line_len = (line >= 0 && line < static_cast<int>(lines.size()))
                             ? static_cast<int>(lines[static_cast<size_t>(line)].size())
                             : 0;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (rows[static_cast<size_t>(i)].line != line) {
            continue;
        }
        last = i;
        const VisualRow& row = rows[static_cast<size_t>(i)];
        if (col >= row.begin && col < row.end) {
            return i;
        }
        if (col == row.end && row.end < line_len && lines[static_cast<size_t>(line)][static_cast<size_t>(col)] == ' ') {
            return i;
        }
    }
    return last;
}

int byte_for_display_col(const std::string& line, const VisualRow& row, int want) {
    if (want <= 0) {
        return row.begin;
    }
    const int slice_end = std::min(row.end, static_cast<int>(line.size()));
    if (row.begin >= slice_end) {
        return row.begin;
    }
    const std::string_view slice(line.data() + row.begin, static_cast<size_t>(slice_end - row.begin));
    return row.begin + offset_for_width(slice, want);
}

}  // namespace inkwell
