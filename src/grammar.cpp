#include "grammar.hpp"

#include "plugin.hpp"
#include "text.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>

namespace inkwell {
namespace {

std::string word_text(const std::string& text, WordSpan word) {
    return ascii_lower(text.substr(word.begin, word.end - word.begin));
}

bool space_between(const std::string& text, WordSpan left, WordSpan right) {
    if (right.begin < left.end) {
        return false;
    }
    if (left.end == right.begin) {
        return false;
    }
    for (size_t i = left.end; i < right.begin; ++i) {
        if (!is_ascii_space(static_cast<unsigned char>(text[i]))) {
            return false;
        }
    }
    return true;
}

std::string apply_case(std::string_view source, std::string replacement) {
    bool any_letter = false;
    bool all_upper = true;
    for (unsigned char c : source) {
        if (c >= 'A' && c <= 'Z') {
            any_letter = true;
        } else if (c >= 'a' && c <= 'z') {
            any_letter = true;
            all_upper = false;
        }
    }
    if (any_letter && all_upper && source.size() > 1) {
        return ascii_upper(replacement);
    }
    if (!source.empty() && source[0] >= 'A' && source[0] <= 'Z' && !replacement.empty() && replacement[0] >= 'a' &&
        replacement[0] <= 'z') {
        replacement[0] = static_cast<char>(replacement[0] - 'a' + 'A');
    }
    return replacement;
}

void add_issue(std::vector<Issue>& issues, size_t begin, size_t end, IssueKind kind, std::string message,
               std::string replacement) {
    if (end < begin) {
        return;
    }
    Issue issue;
    issue.begin = begin;
    issue.end = end;
    issue.kind = kind;
    issue.message = std::move(message);
    if (!replacement.empty() || kind != IssueKind::Spelling) {
        if (!(replacement.empty() && end == begin)) {
            issue.replacements.push_back(std::move(replacement));
            issue.suggestions_ready = true;
        }
    }
    issues.push_back(std::move(issue));
}

bool starts_with(std::string_view word, std::string_view prefix) {
    return word.size() >= prefix.size() && word.substr(0, prefix.size()) == prefix;
}

bool consonant_sound(std::string_view word) {
    static const char* prefixes[] = {"one",    "once",    "eu",      "use",     "usual",   "util",    "utop",
                                     "ufo",    "uk",      "unique",  "unit",    "united",  "union",   "uniform",
                                     "univers","unicorn", "unicycle","unicode", "unison",  "unilat",  "unif"};
    for (const char* prefix : prefixes) {
        if (starts_with(word, prefix)) {
            return true;
        }
    }
    return false;
}

bool silent_h(std::string_view word) {
    static const char* prefixes[] = {"hour", "honest", "honor", "honour", "heir"};
    for (const char* prefix : prefixes) {
        if (starts_with(word, prefix)) {
            return true;
        }
    }
    return false;
}

bool vowel_sound(std::string_view word) {
    if (word.empty()) {
        return false;
    }
    if (silent_h(word)) {
        return true;
    }
    if (consonant_sound(word)) {
        return false;
    }
    const char c = static_cast<char>(word[0]);
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

bool one_of(std::string_view word, std::initializer_list<const char*> options) {
    for (const char* option : options) {
        if (word == option) {
            return true;
        }
    }
    return false;
}

bool abbreviation(std::string_view word) {
    const std::string lower = ascii_lower(word);
    static const char* words[] = {"mr", "mrs", "ms", "dr", "prof", "sr", "jr", "st", "vs", "etc", "eg", "ie",
                                  "am", "pm", "fig", "vol", "no", "dept", "approx", "gen", "col", "sgt"};
    for (const char* item : words) {
        if (lower == item) {
            return true;
        }
    }
    return false;
}

std::string previous_word(const std::string& text, size_t punct) {
    if (punct == 0) {
        return {};
    }
    size_t end = punct;
    while (end > 0 && is_ascii_space(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    size_t begin = end;
    while (begin > 0) {
        const unsigned char c = static_cast<unsigned char>(text[begin - 1]);
        if (std::isalpha(c) == 0) {
            break;
        }
        --begin;
    }
    return text.substr(begin, end - begin);
}

bool overlaps(const Issue& left, const Issue& right) {
    return left.begin < right.end && right.begin < left.end;
}

void keep_unique(std::vector<Issue>& issues) {
    std::sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) {
        if (a.begin != b.begin) {
            return a.begin < b.begin;
        }
        return (a.end - a.begin) > (b.end - b.begin);
    });
    std::vector<Issue> kept;
    for (Issue& issue : issues) {
        bool hit = false;
        for (const Issue& prior : kept) {
            if (overlaps(prior, issue)) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            kept.push_back(std::move(issue));
        }
    }
    issues = std::move(kept);
}

}  // namespace

std::vector<Issue> grammar_issues(const std::string& text) {
    std::vector<Issue> issues;
    const std::vector<WordSpan> words = scan_words(text);
    static const std::unordered_set<std::string> repeated_ok = {"had", "that"};

    for (size_t i = 1; i < words.size(); ++i) {
        if (!space_between(text, words[i - 1], words[i])) {
            continue;
        }
        const std::string left = word_text(text, words[i - 1]);
        const std::string right = word_text(text, words[i]);
        if (left == right && left.size() > 1 && repeated_ok.count(left) == 0) {
            const std::string original = text.substr(words[i - 1].begin, words[i - 1].end - words[i - 1].begin);
            add_issue(issues, words[i - 1].begin, words[i].end, IssueKind::Grammar, "Repeated word", original);
        }
    }

    for (size_t i = 0; i + 1 < words.size(); ++i) {
        if (!space_between(text, words[i], words[i + 1])) {
            continue;
        }
        const std::string a = word_text(text, words[i]);
        const std::string b = word_text(text, words[i + 1]);
        const std::string phrase = text.substr(words[i].begin, words[i + 1].end - words[i].begin);

        if ((a == "a" || a == "an") && !b.empty()) {
            const bool vowel = vowel_sound(b);
            if (a == "a" && vowel) {
                add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Use \"an\" before a vowel sound",
                          apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "an"));
            } else if (a == "an" && !vowel) {
                add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar,
                          "Use \"a\" before a consonant sound",
                          apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "a"));
            }
        }

        if (b == "of" && one_of(a, {"could", "should", "would", "must", "might"})) {
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Grammar, "Use \"have\", not \"of\"",
                      apply_case(phrase, a + " have"));
        }
        if (one_of(b, {"is", "are", "was", "were", "has", "have"}) && a == "their") {
            add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Did you mean \"there\"?",
                      apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "there"));
        }
        if (a == "there" && one_of(b, {"own", "house", "car", "home", "family", "friend", "friends", "child", "children",
                                       "wife", "husband", "mother", "father", "parents", "room", "phone", "job"})) {
            add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Did you mean \"their\"?",
                      apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "their"));
        }
        if (a == "your" && one_of(b, {"welcome", "the", "a", "an", "going", "not"})) {
            add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Did you mean \"you're\"?",
                      apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "you're"));
        }
        if (a == "you're" && one_of(b, {"own", "car", "house", "home", "name", "friend", "friends", "family", "way",
                                        "life", "job", "work", "phone", "room"})) {
            add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Did you mean \"your\"?",
                      apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "your"));
        }
        if (a == "its" && one_of(b, {"a", "an", "the", "been", "going", "not"})) {
            add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Did you mean \"it's\"?",
                      apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "it's"));
        }
        if (a == "it's" && one_of(b, {"own", "color", "colour", "place", "size", "name", "kind", "type", "shape",
                                      "purpose", "meaning"})) {
            add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Did you mean \"its\"?",
                      apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "its"));
        }
        if (b == "don't" && one_of(a, {"he", "she", "it"})) {
            const std::string subject = text.substr(words[i].begin, words[i].end - words[i].begin);
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Grammar, "Use \"doesn't\"",
                      apply_case(phrase, subject + " doesn't"));
        }
        if (b == "was" && one_of(a, {"we", "they"})) {
            const std::string subject = text.substr(words[i].begin, words[i].end - words[i].begin);
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Grammar, "Use \"were\"",
                      apply_case(phrase, subject + " were"));
        }
        if (a == "they" && b == "is") {
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Grammar, "Use \"are\"",
                      apply_case(phrase, "they are"));
        }
        if (a == "i" && b == "are") {
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Grammar, "Use \"am\"",
                      apply_case(phrase, "I am"));
        }
        if (one_of(b, {"better", "worse", "easier", "harder", "faster", "slower", "bigger", "smaller"}) && a == "more") {
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Grammar, "Drop the extra comparison",
                      apply_case(phrase, b));
        }
        if (one_of(b, {"best", "worst", "easiest", "hardest", "biggest", "smallest"}) && a == "most") {
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Grammar, "Drop the extra comparison",
                      apply_case(phrase, b));
        }
        if (a == "less" && one_of(b, {"people", "items", "words", "things", "books", "errors", "mistakes", "options",
                                      "choices", "reasons", "days", "hours", "ideas", "problems", "questions"})) {
            add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Use \"fewer\" for countable things",
                      apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "fewer"));
        }
        if (a == "suppose" && b == "to") {
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Grammar, "Use \"supposed to\"",
                      apply_case(phrase, "supposed to"));
        }
        if (b == "unique" && one_of(a, {"very", "completely", "totally", "absolutely"})) {
            add_issue(issues, words[i].begin, words[i + 1].end, IssueKind::Style, "\"Unique\" doesn't take a modifier",
                      apply_case(phrase, "unique"));
        }

        if (a == "alot") {
            add_issue(issues, words[i].begin, words[i].end, IssueKind::Grammar, "Use two words",
                      apply_case(text.substr(words[i].begin, words[i].end - words[i].begin), "a lot"));
        }
    }

    for (const WordSpan& word : words) {
        const std::string token = word_text(text, word);
        const std::string original = text.substr(word.begin, word.end - word.begin);
        auto fix = [&](const char* replacement, const char* message, IssueKind kind) {
            add_issue(issues, word.begin, word.end, kind, message, apply_case(original, replacement));
        };
        if (original == "i") {
            fix("I", "Capitalize the pronoun", IssueKind::Grammar);
        } else if (original == "i'm") {
            fix("I'm", "Capitalize the pronoun", IssueKind::Grammar);
        } else if (original == "i've") {
            fix("I've", "Capitalize the pronoun", IssueKind::Grammar);
        } else if (original == "i'll") {
            fix("I'll", "Capitalize the pronoun", IssueKind::Grammar);
        } else if (original == "i'd") {
            fix("I'd", "Capitalize the pronoun", IssueKind::Grammar);
        } else if (token == "alot") {
            fix("a lot", "Use two words", IssueKind::Grammar);
        } else if (token == "irregardless") {
            fix("regardless", "\"Irregardless\" isn't a standard word", IssueKind::Grammar);
        } else if (token == "eachother") {
            fix("each other", "Use two words", IssueKind::Grammar);
        } else if (token == "atleast") {
            fix("at least", "Use two words", IssueKind::Grammar);
        } else if (token == "aswell") {
            fix("as well", "Use two words", IssueKind::Grammar);
        } else if (token == "eventhough") {
            fix("even though", "Use two words", IssueKind::Grammar);
        } else if (token == "infront") {
            fix("in front", "Use two words", IssueKind::Grammar);
        }
    }

    for (size_t i = 0; i + 2 < words.size(); ++i) {
        if (word_text(text, words[i]) != "between") {
            continue;
        }
        const size_t last = std::min(words.size() - 1, i + 6);
        for (size_t j = i + 1; j < last; ++j) {
            if (word_text(text, words[j]) == "and" && word_text(text, words[j + 1]) == "i" &&
                space_between(text, words[j], words[j + 1])) {
                add_issue(issues, words[j + 1].begin, words[j + 1].end, IssueKind::Grammar,
                          "After \"between\", use \"me\"", "me");
                break;
            }
        }
    }

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == ' ' && i + 1 < text.size()) {
            const char next = text[i + 1];
            if ((next == '.' || next == ',' || next == '!' || next == '?' || next == ';' || next == ':') && i > 0 &&
                !is_ascii_space(static_cast<unsigned char>(text[i - 1]))) {
                add_issue(issues, i, i + 1, IssueKind::Grammar, "Remove the space before punctuation", "");
            }
        }
        if ((c == '.' || c == '!' || c == '?') && i + 1 < text.size()) {
            size_t j = i + 1;
            while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) {
                ++j;
            }
            if (j >= text.size() || text[j] < 'a' || text[j] > 'z') {
                if (j < text.size() && text[j] >= 'A' && text[j] <= 'Z' && j == i + 1 && i > 0 &&
                    text[i - 1] >= 'a' && text[i - 1] <= 'z') {
                    const std::string before = previous_word(text, i);
                    if (c != '.' || (before.size() > 1 && !abbreviation(before))) {
                        add_issue(issues, i, i + 1, IssueKind::Grammar, "Add a space after the sentence",
                                  std::string(1, c) + " ");
                    }
                }
                continue;
            }
            if (i > 0 && text[i - 1] == '.') {
                continue;
            }
            if (i > 0 && std::isdigit(static_cast<unsigned char>(text[i - 1])) != 0) {
                continue;
            }
            const std::string before = previous_word(text, i);
            if (c == '.' && (before.size() <= 1 || abbreviation(before))) {
                continue;
            }
            size_t end = j;
            while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end])) != 0) {
                ++end;
            }
            if (end == j) {
                continue;
            }
            std::string replacement = text.substr(j, end - j);
            replacement[0] = static_cast<char>(replacement[0] - 'a' + 'A');
            add_issue(issues, j, end, IssueKind::Grammar, "Capitalize the new sentence", std::move(replacement));
        }
    }

    for (size_t i = 0; i < text.size();) {
        if (text[i] != ' ') {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && text[j] == ' ') {
            ++j;
        }
        const bool line_start = i == 0 || text[i - 1] == '\n';
        if (!line_start && j - i >= 3) {
            add_issue(issues, i, j, IssueKind::Style, "Extra spaces", " ");
        }
        i = j;
    }

    keep_unique(issues);
    return issues;
}

std::vector<Issue> analyze_document(const std::string& text, const Dictionary& dictionary) {
    std::vector<Issue> issues = grammar_issues(text);
    std::vector<Issue> spelling = spelling_issues(text, dictionary);
    for (Issue& issue : spelling) {
        bool hit = false;
        for (const Issue& prior : issues) {
            if (overlaps(prior, issue)) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            issues.push_back(std::move(issue));
        }
    }
    append_plugin_issues(text, issues);
    std::sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) { return a.begin < b.begin; });
    return issues;
}

}  // namespace inkwell
