#include "markup.hpp"

#include "utf8.hpp"

#include <cctype>

namespace inkwell {
namespace {

void add_run(std::vector<MarkupRun>& runs, std::string text, bool italic, bool bold) {
    if (text.empty()) {
        return;
    }
    if (!runs.empty() && runs.back().italic == italic && runs.back().bold == bold) {
        runs.back().text += text;
        return;
    }
    runs.push_back(MarkupRun{std::move(text), italic, bold});
}

std::string escape_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string escape_attr(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '<':
                out += "&lt;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

bool starts_with(std::string_view text, size_t index, std::string_view tag) {
    if (index + tag.size() > text.size()) {
        return false;
    }
    for (size_t i = 0; i < tag.size(); ++i) {
        const char have = static_cast<char>(std::tolower(static_cast<unsigned char>(text[index + i])));
        if (have != tag[i]) {
            return false;
        }
    }
    return true;
}

size_t skip_tag(std::string_view text, size_t index) {
    while (index < text.size() && text[index] != '>') {
        ++index;
    }
    return index < text.size() ? index + 1 : index;
}

std::string attr_value(std::string_view tag, std::string_view key) {
    const std::string pattern = std::string(key) + "=";
    for (size_t i = 0; i + pattern.size() < tag.size(); ++i) {
        bool match = true;
        for (size_t n = 0; n < pattern.size(); ++n) {
            const char have = static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i + n])));
            if (have != pattern[n]) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }
        size_t value = i + pattern.size();
        if (value >= tag.size()) {
            return {};
        }
        const char quote = tag[value];
        if (quote == '"' || quote == '\'') {
            ++value;
            const size_t end = tag.find(quote, value);
            return std::string(tag.substr(value, end == std::string_view::npos ? tag.size() - value : end - value));
        }
        size_t end = value;
        while (end < tag.size() && std::isspace(static_cast<unsigned char>(tag[end])) == 0 && tag[end] != '>') {
            ++end;
        }
        return std::string(tag.substr(value, end - value));
    }
    return {};
}

void append_entity(std::string& out, std::string_view entity) {
    if (entity == "lt") {
        out.push_back('<');
    } else if (entity == "gt") {
        out.push_back('>');
    } else if (entity == "amp") {
        out.push_back('&');
    } else if (entity == "quot") {
        out.push_back('"');
    } else if (entity == "apos") {
        out.push_back('\'');
    } else if (entity == "nbsp") {
        out.push_back(' ');
    } else if (!entity.empty() && entity[0] == '#') {
        int code = 0;
        if (entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')) {
            for (size_t i = 2; i < entity.size(); ++i) {
                const char h = entity[i];
                code *= 16;
                if (h >= '0' && h <= '9') {
                    code += h - '0';
                } else if (h >= 'a' && h <= 'f') {
                    code += h - 'a' + 10;
                } else if (h >= 'A' && h <= 'F') {
                    code += h - 'A' + 10;
                }
            }
        } else {
            for (size_t i = 1; i < entity.size(); ++i) {
                if (entity[i] >= '0' && entity[i] <= '9') {
                    code = code * 10 + entity[i] - '0';
                }
            }
        }
        if (code > 0) {
            append_utf8(out, static_cast<char32_t>(code));
        }
    }
}

std::vector<MarkupRun> parse_inlines(std::string_view inner) {
    std::vector<MarkupRun> runs;
    bool italic = false;
    bool bold = false;
    std::string current;
    auto flush = [&] {
        add_run(runs, std::move(current), italic, bold);
        current.clear();
    };
    for (size_t i = 0; i < inner.size();) {
        if (inner[i] == '&') {
            const size_t end = inner.find(';', i + 1);
            if (end != std::string_view::npos && end - i < 12) {
                flush();
                append_entity(current, inner.substr(i + 1, end - i - 1));
                add_run(runs, std::move(current), italic, bold);
                current.clear();
                i = end + 1;
                continue;
            }
        }
        if (inner[i] == '<') {
            flush();
            if (starts_with(inner, i, "<br")) {
                add_run(runs, "\n", italic, bold);
                i = skip_tag(inner, i);
                continue;
            }
            if (starts_with(inner, i, "<i>") || starts_with(inner, i, "<em>") || starts_with(inner, i, "<em ")) {
                italic = true;
                i = skip_tag(inner, i);
                continue;
            }
            if (starts_with(inner, i, "</i>") || starts_with(inner, i, "</em>")) {
                italic = false;
                i = skip_tag(inner, i);
                continue;
            }
            if (starts_with(inner, i, "<b>") || starts_with(inner, i, "<strong>") || starts_with(inner, i, "<strong ")) {
                bold = true;
                i = skip_tag(inner, i);
                continue;
            }
            if (starts_with(inner, i, "</b>") || starts_with(inner, i, "</strong>")) {
                bold = false;
                i = skip_tag(inner, i);
                continue;
            }
            i = skip_tag(inner, i);
            continue;
        }
        if (inner[i] == '\n' || inner[i] == '\r') {
            if (inner[i] == '\r' && i + 1 < inner.size() && inner[i + 1] == '\n') {
                ++i;
            }
            if (current.empty() || current.back() != ' ') {
                current.push_back(' ');
            }
            ++i;
            continue;
        }
        current.push_back(inner[i]);
        ++i;
    }
    flush();
    return runs;
}

std::string block_text(const MarkupBlock& block) {
    std::string out;
    for (const MarkupRun& run : block.runs) {
        if (!run.text.empty()) {
            if (run.italic) {
                out += "<i>";
            }
            if (run.bold) {
                out += "<b>";
            }
            out += escape_text(run.text);
            if (run.bold) {
                out += "</b>";
            }
            if (run.italic) {
                out += "</i>";
            }
        }
    }
    return out;
}

bool is_scene_line(std::string_view line) {
    return line == "***" || line == "* * *" || line == "---";
}

std::vector<MarkupBlock> plain_blocks(std::string_view text) {
    std::vector<MarkupBlock> blocks;
    size_t i = 0;
    while (i < text.size()) {
        size_t end = text.find('\n', i);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(i, end - i);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        MarkupBlock block;
        if (is_scene_line(line)) {
            block.kind = MarkupBlock::Kind::Scene;
        } else if (!line.empty()) {
            block.runs.push_back(MarkupRun{std::string(line), false, false});
        }
        blocks.push_back(std::move(block));
        i = end < text.size() ? end + 1 : text.size();
    }
    return blocks;
}

std::string_view trim_line(std::string_view line) {
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())) != 0) {
        line.remove_prefix(1);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
        line.remove_suffix(1);
    }
    return line;
}

MarkupBlock parse_line(std::string_view line) {
    const std::string_view trimmed = trim_line(line);
    MarkupBlock block;
    if (trimmed.empty()) {
        return block;
    }
    if (is_scene_line(trimmed) || starts_with(trimmed, 0, "<hr")) {
        block.kind = MarkupBlock::Kind::Scene;
        return block;
    }
    if (starts_with(trimmed, 0, "<img")) {
        block.kind = MarkupBlock::Kind::Image;
        block.src = attr_value(trimmed, "src");
        block.name = attr_value(trimmed, "alt");
        return block;
    }
    if (starts_with(trimmed, 0, "<field")) {
        const size_t open = trimmed.find('>');
        const std::string_view tag = open == std::string_view::npos ? trimmed : trimmed.substr(0, open);
        std::string_view inner;
        if (open != std::string_view::npos && open + 1 < trimmed.size()) {
            inner = trimmed.substr(open + 1);
            const size_t close = inner.rfind("</field>");
            if (close != std::string_view::npos) {
                inner = inner.substr(0, close);
            }
        }
        block.kind = MarkupBlock::Kind::Field;
        block.name = attr_value(tag, "name");
        block.runs = parse_inlines(inner);
        return block;
    }
    std::string_view prose = line;
    if (starts_with(trimmed, 0, "<p>") || starts_with(trimmed, 0, "<p ") || starts_with(trimmed, 0, "<p/>")) {
        const size_t open = trimmed.find('>');
        if (open != std::string_view::npos && open + 1 <= trimmed.size()) {
            prose = trimmed.substr(open + 1);
            const size_t close = prose.rfind("</p>");
            if (close != std::string_view::npos) {
                prose = prose.substr(0, close);
            }
        } else {
            prose = {};
        }
    }
    block.runs = parse_inlines(prose);
    return block;
}

std::vector<MarkupBlock> marked_blocks(std::string_view text) {
    std::vector<MarkupBlock> blocks;
    size_t i = 0;
    while (i < text.size()) {
        size_t end = text.find('\n', i);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(i, end - i);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        blocks.push_back(parse_line(line));
        i = end < text.size() ? end + 1 : text.size();
    }
    if (!text.empty() && (text.back() == '\n' || text.back() == '\r') && !blocks.empty() && blocks.back().kind == MarkupBlock::Kind::Paragraph &&
        blocks.back().runs.empty()) {
        blocks.pop_back();
    }
    return blocks;
}

}  // namespace

std::vector<MarkupBlock> parse_markup(std::string_view text) {
    const bool marked = text.find("<p") != std::string_view::npos || text.find("<P") != std::string_view::npos ||
                        text.find("<img") != std::string_view::npos || text.find("<IMG") != std::string_view::npos ||
                        text.find("<field") != std::string_view::npos || text.find("<hr") != std::string_view::npos ||
                        text.find("<HR") != std::string_view::npos || text.find("<i>") != std::string_view::npos ||
                        text.find("<b>") != std::string_view::npos || text.find("<em>") != std::string_view::npos ||
                        text.find("<strong>") != std::string_view::npos;
    if (!marked) {
        return plain_blocks(text);
    }
    return marked_blocks(text);
}

bool block_needs_tags(const MarkupBlock& block) {
    if (block.kind == MarkupBlock::Kind::Image || block.kind == MarkupBlock::Kind::Field) {
        return true;
    }
    std::string text;
    for (const MarkupRun& run : block.runs) {
        if (run.italic || run.bold) {
            return true;
        }
        text += run.text;
    }
    return block.kind == MarkupBlock::Kind::Paragraph && is_scene_line(text);
}

std::string emit_plain(const std::vector<MarkupBlock>& blocks) {
    std::string out;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i != 0) {
            out.push_back('\n');
        }
        const MarkupBlock& block = blocks[i];
        if (block.kind == MarkupBlock::Kind::Scene) {
            out += "* * *";
            continue;
        }
        for (const MarkupRun& run : block.runs) {
            out += run.text;
        }
    }
    return out;
}

std::string emit_markup(const std::vector<MarkupBlock>& blocks) {
    bool rich = false;
    for (const MarkupBlock& block : blocks) {
        if (block_needs_tags(block)) {
            rich = true;
            break;
        }
    }
    if (!rich) {
        return emit_plain(blocks);
    }
    std::string out;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i != 0) {
            out.push_back('\n');
        }
        const MarkupBlock& block = blocks[i];
        switch (block.kind) {
            case MarkupBlock::Kind::Scene:
                out += "* * *";
                break;
            case MarkupBlock::Kind::Image:
                out += "<img src=\"" + escape_attr(block.src) + "\"";
                if (!block.name.empty()) {
                    out += " alt=\"" + escape_attr(block.name) + "\"";
                }
                out += ">";
                break;
            case MarkupBlock::Kind::Field:
                out += "<field name=\"" + escape_attr(block.name) + "\">" + block_text(block) + "</field>";
                break;
            case MarkupBlock::Kind::Paragraph: {
                std::string text;
                for (const MarkupRun& run : block.runs) {
                    text += run.text;
                }
                const std::string body = block_text(block);
                if (is_scene_line(text)) {
                    out += "<p>" + body + "</p>";
                } else {
                    out += body;
                }
                break;
            }
        }
    }
    return out;
}

std::string markup_plain(std::string_view text) {
    std::string out;
    for (const MarkupBlock& block : parse_markup(text)) {
        if (!out.empty()) {
            out.push_back('\n');
        }
        if (block.kind == MarkupBlock::Kind::Image) {
            out += block.name.empty() ? block.src : block.name;
            continue;
        }
        if (block.kind == MarkupBlock::Kind::Scene) {
            continue;
        }
        for (const MarkupRun& run : block.runs) {
            out += run.text;
        }
    }
    return out;
}

}  // namespace inkwell
