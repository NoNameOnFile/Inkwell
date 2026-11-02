#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace inkwell {

struct MarkupRun {
    std::string text;
    bool italic = false;
    bool bold = false;
};

struct MarkupBlock {
    enum class Kind { Paragraph, Scene, Field, Image };

    Kind kind = Kind::Paragraph;
    std::string name;
    std::string src;
    std::vector<MarkupRun> runs;
};

std::vector<MarkupBlock> parse_markup(std::string_view text);
std::string emit_markup(const std::vector<MarkupBlock>& blocks);
std::string markup_plain(std::string_view text);

}  // namespace inkwell
