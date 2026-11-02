#pragma once

#include <string>
#include <vector>

namespace inkwell {

struct VisualRow {
    int line = 0;
    int begin = 0;
    int end = 0;
};

std::vector<VisualRow> wrap_document(const std::vector<std::string>& lines, int width);
int row_for_cursor(const std::vector<VisualRow>& rows, const std::vector<std::string>& lines, int line, int col);
int byte_for_display_col(const std::string& line, const VisualRow& row, int want);

}  // namespace inkwell
