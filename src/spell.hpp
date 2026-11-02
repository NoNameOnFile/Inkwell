#pragma once

#include "issues.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace inkwell {

class Dictionary {
public:
    bool load(std::string& error);
    bool check(std::string_view word) const;
    std::vector<std::string> suggest(std::string_view word, int limit = 6, bool deep = true) const;
    bool learn(const std::string& word, std::string& error);
    void ignore(std::string_view word);
    bool ready() const { return ready_; }
    const std::string& source() const { return source_; }

private:
    struct Entry {
        unsigned accept = 0;
        bool nosuggest = false;
        std::string display;
    };
    struct Rep {
        std::string from;
        std::string to;
    };

    bool check_single(const std::string& word) const;
    std::vector<std::string> suggest_single(const std::string& word, int limit, bool deep) const;
    void add_form(std::string surface, bool nosuggest);
    static std::string present_entry(const Entry& entry, std::string_view typed);

    bool ready_ = false;
    std::string source_;
    std::string personal_path_;
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_set<std::string> exact_;
    std::unordered_set<std::string> ignored_;
    std::vector<Rep> reps_;
    mutable std::unordered_map<std::string, std::vector<std::string>> cache_;
};

std::vector<Issue> spelling_issues(const std::string& text, const Dictionary& dictionary);

}  // namespace inkwell
