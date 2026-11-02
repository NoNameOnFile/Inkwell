#include "lint.hpp"

#include "editor.hpp"
#include "grammar.hpp"
#include "spell.hpp"
#include "text.hpp"

#include <iostream>

namespace inkwell {
namespace {

std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += parts[i].empty() ? "delete" : parts[i];
    }
    return out;
}

int lint_one(const std::string& path, Dictionary& dictionary) {
    Editor editor;
    std::string error;
    if (!editor.load_file(path, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    const std::string text = editor.text();
    std::vector<Issue> issues = analyze_document(text, dictionary);
    for (Issue& issue : issues) {
        if (issue.kind == IssueKind::Spelling && issue.end >= issue.begin && issue.end <= text.size()) {
            issue.replacements = dictionary.suggest(text.substr(issue.begin, issue.end - issue.begin), 5, false);
        }
        int line = 1;
        int column = 1;
        locate_offset(text, issue.begin, line, column);
        std::cout << path << ':' << line << ':' << column << ": " << issue_kind_name(issue.kind) << ": "
                  << snippet(text, issue.begin, issue.end, 60);
        if (!issue.replacements.empty()) {
            std::cout << " -> " << joined(issue.replacements);
        } else if (!issue.message.empty()) {
            std::cout << " (" << issue.message << ')';
        }
        std::cout << '\n';
    }
    return issues.empty() ? 0 : 1;
}

}  // namespace

int lint_paths(const std::vector<std::string>& paths) {
    if (paths.empty()) {
        std::cerr << "Name a file to check.\n";
        return 2;
    }
    Dictionary dictionary;
    std::string error;
    if (!dictionary.load(error)) {
        std::cerr << error << '\n';
        return 2;
    }
    int status = 0;
    for (const std::string& path : paths) {
        const int one = lint_one(path, dictionary);
        if (one == 2) {
            return 2;
        }
        if (one != 0) {
            status = 1;
        }
    }
    return status;
}

}  // namespace inkwell
