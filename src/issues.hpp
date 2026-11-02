#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace inkwell {

enum class IssueKind { Spelling, Grammar, Style, Name };

struct Issue {
    size_t begin = 0;
    size_t end = 0;
    IssueKind kind = IssueKind::Grammar;
    std::string message;
    std::vector<std::string> replacements;
    bool suggestions_ready = false;
};

inline const char* issue_kind_name(IssueKind kind) {
    switch (kind) {
        case IssueKind::Spelling:
            return "spelling";
        case IssueKind::Grammar:
            return "grammar";
        case IssueKind::Style:
            return "style";
        case IssueKind::Name:
            return "name";
    }
    return "note";
}

}  // namespace inkwell
