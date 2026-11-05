#include "spell.hpp"

#if defined(_WIN32)
#include "hostpath.hpp"
#endif
#include "text.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace inkwell {
namespace {

constexpr unsigned kLow = 1;
constexpr unsigned kCap = 2;
constexpr unsigned kUp = 4;

enum class CaseForm { Lower, Capital, Upper, Mixed };

struct Atom {
    bool any = false;
    bool neg = false;
    std::string chars;
};

struct Affix {
    bool cross = false;
    std::string strip;
    std::string add;
    std::vector<Atom> cond;
};

CaseForm classify(std::string_view word) {
    bool lower = false;
    bool upper = false;
    char first_alpha = 0;
    for (unsigned char c : word) {
        if (c >= 'A' && c <= 'Z') {
            upper = true;
            if (first_alpha == 0) {
                first_alpha = static_cast<char>(c);
            }
        } else if (c >= 'a' && c <= 'z') {
            lower = true;
            if (first_alpha == 0) {
                first_alpha = static_cast<char>(c);
            }
        }
    }
    if (!upper) {
        return CaseForm::Lower;
    }
    if (!lower) {
        return CaseForm::Upper;
    }
    if (first_alpha >= 'A' && first_alpha <= 'Z') {
        return CaseForm::Capital;
    }
    return CaseForm::Mixed;
}

unsigned accept_bits(std::string_view word) {
    switch (classify(word)) {
        case CaseForm::Lower:
            return kLow | kCap | kUp;
        case CaseForm::Capital:
            return kCap | kUp;
        case CaseForm::Upper:
            return kUp;
        case CaseForm::Mixed:
            return 0;
    }
    return 0;
}

std::vector<Atom> parse_cond(const std::string& pattern) {
    std::vector<Atom> atoms;
    for (size_t i = 0; i < pattern.size();) {
        if (pattern[i] == '[') {
            Atom atom;
            ++i;
            if (i < pattern.size() && pattern[i] == '^') {
                atom.neg = true;
                ++i;
            }
            while (i < pattern.size() && pattern[i] != ']') {
                atom.chars.push_back(pattern[i]);
                ++i;
            }
            if (i < pattern.size() && pattern[i] == ']') {
                ++i;
            }
            atoms.push_back(std::move(atom));
        } else if (pattern[i] == '.') {
            atoms.push_back(Atom{true, false, {}});
            ++i;
        } else {
            Atom atom;
            atom.chars = std::string(1, pattern[i]);
            atoms.push_back(std::move(atom));
            ++i;
        }
    }
    return atoms;
}

bool match_side(std::string_view word, const std::vector<Atom>& cond, bool at_end) {
    if (cond.empty()) {
        return true;
    }
    if (word.size() < cond.size()) {
        return false;
    }
    const size_t start = at_end ? word.size() - cond.size() : 0;
    for (size_t i = 0; i < cond.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(word[start + i]);
        const Atom& atom = cond[i];
        if (atom.any) {
            continue;
        }
        const bool found = atom.chars.find(static_cast<char>(c)) != std::string::npos;
        if (atom.neg) {
            if (found) {
                return false;
            }
        } else if (!found) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> apply_suffix(std::string_view stem, const Affix& rule) {
    if (!match_side(stem, rule.cond, true)) {
        return std::nullopt;
    }
    if (!rule.strip.empty()) {
        if (stem.size() < rule.strip.size() || stem.substr(stem.size() - rule.strip.size()) != rule.strip) {
            return std::nullopt;
        }
    }
    std::string out(stem.substr(0, stem.size() - rule.strip.size()));
    out += rule.add;
    return out;
}

std::optional<std::string> apply_prefix(std::string_view stem, const Affix& rule) {
    if (!match_side(stem, rule.cond, false)) {
        return std::nullopt;
    }
    if (!rule.strip.empty()) {
        if (stem.size() < rule.strip.size() || stem.substr(0, rule.strip.size()) != rule.strip) {
            return std::nullopt;
        }
    }
    std::string out = rule.add;
    out.append(stem.substr(rule.strip.size()));
    return out;
}

std::optional<std::string> apply_cross(std::string_view stem, const Affix& prefix, const Affix& suffix) {
    if (stem.size() < prefix.strip.size() + suffix.strip.size()) {
        return std::nullopt;
    }
    if (!prefix.strip.empty() && stem.substr(0, prefix.strip.size()) != prefix.strip) {
        return std::nullopt;
    }
    if (!suffix.strip.empty() &&
        stem.substr(stem.size() - suffix.strip.size()) != suffix.strip) {
        return std::nullopt;
    }
    std::string out = prefix.add;
    out.append(stem.substr(prefix.strip.size(), stem.size() - prefix.strip.size() - suffix.strip.size()));
    out += suffix.add;
    return out;
}

std::vector<std::string> fields_of(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                fields.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        fields.push_back(std::move(current));
    }
    return fields;
}

std::string unescape_dic(std::string_view word) {
    std::string out;
    out.reserve(word.size());
    for (size_t i = 0; i < word.size(); ++i) {
        if (word[i] == '\\' && i + 1 < word.size()) {
            out.push_back(word[++i]);
        } else {
            out.push_back(word[i]);
        }
    }
    return out;
}

bool all_digits(std::string_view word) {
    if (word.empty()) {
        return false;
    }
    for (unsigned char c : word) {
        if (std::isdigit(c) == 0) {
            return false;
        }
    }
    return true;
}

bool is_ordinal(std::string_view word) {
    size_t i = 0;
    while (i < word.size() && std::isdigit(static_cast<unsigned char>(word[i])) != 0) {
        ++i;
    }
    if (i == 0 || i == word.size()) {
        return false;
    }
    const std::string_view suffix = word.substr(i);
    return suffix == "st" || suffix == "nd" || suffix == "rd" || suffix == "th";
}

bool valid_roman(const std::string& word) {
    if (word.size() < 2 || word.size() > 15) {
        return false;
    }
    static const std::pair<const char*, int> parts[] = {
        {"M", 4}, {"CM", 1}, {"D", 1}, {"CD", 1}, {"C", 3}, {"XC", 1},
        {"L", 1}, {"XL", 1}, {"X", 3}, {"IX", 1}, {"V", 1}, {"IV", 1}, {"I", 3},
    };
    size_t index = 0;
    for (const auto& [symbol, max_rep] : parts) {
        const size_t len = std::char_traits<char>::length(symbol);
        int reps = 0;
        while (reps < max_rep && index + len <= word.size() && word.compare(index, len, symbol) == 0) {
            index += len;
            ++reps;
        }
    }
    return index == word.size();
}

std::string normalize_word(std::string_view word, bool& foreign) {
    foreign = false;
    std::string out;
    out.reserve(word.size());
    for (size_t i = 0; i < word.size();) {
        const auto c = static_cast<unsigned char>(word[i]);
        if (c == 0xE2 && i + 2 < word.size() && static_cast<unsigned char>(word[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(word[i + 2]) == 0x99 || static_cast<unsigned char>(word[i + 2]) == 0x98)) {
            out.push_back('\'');
            i += 3;
            continue;
        }
        if (c >= 128) {
            foreign = true;
            return {};
        }
        out.push_back(static_cast<char>(c));
        ++i;
    }
    return out;
}

const char* kExtra[] = {
    "ok",     "wifi",      "blog",       "blogs",     "blogger",   "podcast",
    "podcasts", "emoji",   "emojis",     "screenshot","screenshots","startup",
    "startups","username", "online",     "offline",   "inbox",     "todo",
    "covid",  "livestream","hashtag",    "hashtags",  "ai",        "url",
    "pdf",    "html",      "css",        "api",       "app",       "apps",
};

void add_dict_base(std::vector<std::string>& candidates, const std::string& base) {
    if (!base.empty()) {
        candidates.push_back(base);
    }
}

#if defined(_WIN32)
void add_windows_dict_roots(std::vector<std::filesystem::path>& roots) {
    const std::filesystem::path executable = executable_path();
    if (!executable.empty()) {
        const std::filesystem::path bindir = executable.parent_path();
        roots.push_back(bindir / "hunspell");
        roots.push_back(bindir);
    }
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        if (local[0] != '\0') {
            roots.push_back(std::filesystem::path(local) / "inkwell" / "hunspell");
        }
    } else if (const char* roaming = std::getenv("APPDATA")) {
        if (roaming[0] != '\0') {
            roots.push_back(std::filesystem::path(roaming) / "inkwell" / "hunspell");
        }
    }
}

void add_named_dicts(std::vector<std::string>& candidates, const std::vector<std::filesystem::path>& roots, const std::string& name) {
    if (name.empty()) {
        return;
    }
    for (const std::filesystem::path& root : roots) {
        add_dict_base(candidates, (root / name).string());
    }
}
#endif

std::optional<std::pair<std::string, std::string>> find_dict_files(std::string& error) {
    std::vector<std::string> candidates;
    std::string missing = "No Hunspell dictionary found. Expected /usr/share/hunspell/en_US.aff and en_US.dic.";
#if defined(_WIN32)
    missing = "No Hunspell dictionary found. Expected hunspell\\en_US.aff and en_US.dic beside the program, "
              "or %LOCALAPPDATA%\\inkwell\\hunspell\\en_US.aff and en_US.dic.";
    std::string preferred;
    if (const char* env = std::getenv("INKWELL_DICT")) {
        const std::string value(env);
        if (!value.empty()) {
            if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos) {
                candidates.push_back(value);
            } else {
                preferred = value;
            }
        }
    }
    std::vector<std::filesystem::path> roots;
    add_windows_dict_roots(roots);
    if (!preferred.empty()) {
        add_named_dicts(candidates, roots, preferred);
    }
    add_named_dicts(candidates, roots, "en_US");
    add_named_dicts(candidates, roots, "en_GB");
#else
    if (const char* env = std::getenv("INKWELL_DICT")) {
        if (std::string(env).find('/') == std::string::npos) {
            candidates.push_back(std::string("/usr/share/hunspell/") + env);
        } else {
            candidates.push_back(env);
        }
    }
    if (const char* data_dirs = std::getenv("XDG_DATA_DIRS")) {
        std::string rest(data_dirs);
        while (!rest.empty()) {
            const std::size_t colon = rest.find(':');
            const std::string dir = rest.substr(0, colon);
            add_dict_base(candidates, dir.empty() ? std::string() : dir + "/hunspell/en_US");
            add_dict_base(candidates, dir.empty() ? std::string() : dir + "/hunspell/en_GB");
            if (colon == std::string::npos) {
                break;
            }
            rest.erase(0, colon + 1);
        }
    }
    candidates.emplace_back("/app/share/hunspell/en_US");
    candidates.emplace_back("/usr/share/hunspell/en_US");
    candidates.emplace_back("/usr/share/hunspell/en_GB");
#endif
    for (const std::string& base : candidates) {
        namespace fs = std::filesystem;
        std::string aff = base + ".aff";
        std::string dic = base + ".dic";
        std::error_code ec;
        if (fs::is_directory(base, ec)) {
            if (fs::exists(base + "/en_US.aff", ec)) {
                aff = base + "/en_US.aff";
                dic = base + "/en_US.dic";
            }
        }
        if (fs::exists(aff, ec) && fs::exists(dic, ec)) {
            return std::make_pair(aff, dic);
        }
    }
    error = std::move(missing);
    return std::nullopt;
}

std::string personal_path() {
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        if (local[0] != '\0') {
            return (std::filesystem::path(local) / "inkwell" / "personal.dic").string();
        }
    }
    if (const char* roaming = std::getenv("APPDATA")) {
        if (roaming[0] != '\0') {
            return (std::filesystem::path(roaming) / "inkwell" / "personal.dic").string();
        }
    }
#else
    if (const char* home = std::getenv("HOME")) {
        return std::string(home) + "/.config/inkwell/personal.dic";
    }
#endif
    return "inkwell-personal.dic";
}

template <typename Fn>
bool each_edit1(const std::string& word, Fn&& fn) {
    static const char* alphabet = "abcdefghijklmnopqrstuvwxyz'";
    const size_t n = word.size();
    for (size_t i = 0; i < n; ++i) {
        if (!fn(word.substr(0, i) + word.substr(i + 1))) {
            return false;
        }
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        if (word[i] == word[i + 1]) {
            continue;
        }
        std::string swapped = word;
        std::swap(swapped[i], swapped[i + 1]);
        if (!fn(std::move(swapped))) {
            return false;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        for (const char* p = alphabet; *p != '\0'; ++p) {
            if (*p == word[i]) {
                continue;
            }
            std::string replaced = word;
            replaced[i] = *p;
            if (!fn(std::move(replaced))) {
                return false;
            }
        }
    }
    for (size_t i = 0; i <= n; ++i) {
        for (const char* p = alphabet; *p != '\0'; ++p) {
            std::string inserted = word.substr(0, i);
            inserted.push_back(*p);
            inserted += word.substr(i);
            if (!fn(std::move(inserted))) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

void Dictionary::add_form(std::string surface, bool nosuggest) {
    if (surface.empty() || surface.size() > 80) {
        return;
    }
    const CaseForm form = classify(surface);
    if (form == CaseForm::Mixed) {
        exact_.insert(surface);
    }
    const unsigned bits = accept_bits(surface);
    const std::string key = ascii_lower(surface);
    auto [it, inserted] = entries_.try_emplace(key);
    Entry& entry = it->second;
    if (inserted) {
        entry.accept = bits;
        entry.nosuggest = nosuggest;
        entry.display = std::move(surface);
        return;
    }
    entry.accept |= bits;
    if (!nosuggest) {
        entry.nosuggest = false;
    }
    if ((bits & kLow) != 0) {
        entry.display = key;
    }
}

bool Dictionary::load(std::string& error) {
    entries_.clear();
    exact_.clear();
    reps_.clear();
    cache_.clear();
    entries_.reserve(500000);

    const auto paths = find_dict_files(error);
    if (!paths) {
        return false;
    }
    std::ifstream aff_in{std::filesystem::path(paths->first)};
    if (!aff_in) {
        error = "Could not read " + paths->first;
        return false;
    }

    std::vector<Affix> prefixes[128];
    std::vector<Affix> suffixes[128];
    bool header_cross = false;
    char header_flag = 0;
    char compound_flag = 'c';
    char nosuggest_flag = '!';

    std::string line;
    while (std::getline(aff_in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> fields = fields_of(line);
        if (fields.empty()) {
            continue;
        }
        if (fields[0] == "ONLYINCOMPOUND" && fields.size() >= 2 && !fields[1].empty()) {
            compound_flag = fields[1][0];
            continue;
        }
        if (fields[0] == "NOSUGGEST" && fields.size() >= 2 && !fields[1].empty()) {
            nosuggest_flag = fields[1][0];
            continue;
        }
        if (fields[0] == "REP" && fields.size() >= 3) {
            std::string to = fields[2];
            for (char& c : to) {
                if (c == '_') {
                    c = ' ';
                }
            }
            reps_.push_back(Rep{fields[1], std::move(to)});
            continue;
        }
        if ((fields[0] == "PFX" || fields[0] == "SFX") && fields.size() == 4 && !fields[1].empty()) {
            header_flag = fields[1][0];
            header_cross = fields[2] == "Y";
            continue;
        }
        if ((fields[0] == "PFX" || fields[0] == "SFX") && fields.size() >= 5 && !fields[1].empty()) {
            Affix rule;
            rule.cross = fields[1][0] == header_flag && header_cross;
            rule.strip = fields[2] == "0" ? "" : fields[2];
            rule.add = fields[3] == "0" ? "" : fields[3];
            rule.cond = parse_cond(fields[4]);
            const unsigned char flag = static_cast<unsigned char>(fields[1][0]);
            if (flag < 128) {
                if (fields[0] == "PFX") {
                    prefixes[flag].push_back(std::move(rule));
                } else {
                    suffixes[flag].push_back(std::move(rule));
                }
            }
        }
    }

    std::ifstream dic_in{std::filesystem::path(paths->second)};
    if (!dic_in) {
        error = "Could not read " + paths->second;
        return false;
    }
    std::getline(dic_in, line);
    while (std::getline(dic_in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            line.resize(tab);
        }
        const std::string raw = unescape_dic(line);
        const size_t slash = raw.find('/');
        std::string stem = slash == std::string::npos ? raw : raw.substr(0, slash);
        std::string flags = slash == std::string::npos ? "" : raw.substr(slash + 1);
        if (stem.empty()) {
            continue;
        }
        if (flags.find(compound_flag) != std::string::npos) {
            continue;
        }
        const bool nosuggest = flags.find(nosuggest_flag) != std::string::npos;
        add_form(stem, nosuggest);

        struct Made {
            std::string form;
            bool cross = false;
            Affix rule;
        };
        std::vector<Made> suffixed;
        for (unsigned char flag : flags) {
            if (flag >= 128) {
                continue;
            }
            for (const Affix& rule : suffixes[flag]) {
                if (auto form = apply_suffix(stem, rule)) {
                    add_form(*form, nosuggest);
                    if (rule.cross) {
                        suffixed.push_back(Made{*form, true, rule});
                    }
                }
            }
        }
        for (unsigned char flag : flags) {
            if (flag >= 128) {
                continue;
            }
            for (const Affix& rule : prefixes[flag]) {
                if (!apply_prefix(stem, rule)) {
                    continue;
                }
                if (auto form = apply_prefix(stem, rule)) {
                    add_form(*form, nosuggest);
                }
                if (!rule.cross) {
                    continue;
                }
                for (const Made& made : suffixed) {
                    if (!made.cross) {
                        continue;
                    }
                    if (auto crossed = apply_cross(stem, rule, made.rule)) {
                        add_form(*crossed, nosuggest);
                    }
                }
            }
        }
    }

    for (const char* word : kExtra) {
        add_form(word, false);
    }

    personal_path_ = personal_path();
    std::ifstream personal{std::filesystem::path(personal_path_)};
    if (personal) {
        std::string word;
        while (std::getline(personal, word)) {
            if (!word.empty() && word.back() == '\r') {
                word.pop_back();
            }
            if (word.empty() || word[0] == '#') {
                continue;
            }
            add_form(word, false);
        }
    }

    source_ = paths->second;
    ready_ = true;
    return true;
}

bool Dictionary::check_single(const std::string& word) const {
    if (word.empty()) {
        return true;
    }
    if (ignored_.count(ascii_lower(word)) != 0) {
        return true;
    }
    if (exact_.count(word) != 0) {
        return true;
    }
    if (all_digits(word) || is_ordinal(word)) {
        return true;
    }
    const std::string key = ascii_lower(word);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        if (classify(word) == CaseForm::Upper && valid_roman(word)) {
            return true;
        }
        return false;
    }
    switch (classify(word)) {
        case CaseForm::Lower:
            return (it->second.accept & kLow) != 0;
        case CaseForm::Capital:
            return (it->second.accept & kCap) != 0;
        case CaseForm::Upper:
            return (it->second.accept & kUp) != 0;
        case CaseForm::Mixed:
            return false;
    }
    return false;
}

bool Dictionary::check(std::string_view word) const {
    if (!ready_) {
        return true;
    }
    bool foreign = false;
    const std::string normalized = normalize_word(word, foreign);
    if (foreign) {
        return true;
    }
    if (check_single(normalized)) {
        return true;
    }
    if (normalized.find('-') == std::string::npos) {
        return false;
    }
    size_t start = 0;
    bool any = false;
    while (start <= normalized.size()) {
        const size_t dash = normalized.find('-', start);
        const std::string part = normalized.substr(start, dash == std::string::npos ? std::string::npos : dash - start);
        if (part.empty() || (!check_single(part) && !all_digits(part))) {
            return false;
        }
        any = true;
        if (dash == std::string::npos) {
            break;
        }
        start = dash + 1;
    }
    return any;
}

std::string Dictionary::present_entry(const Entry& entry, std::string_view typed) {
    const CaseForm form = classify(typed);
    if (form == CaseForm::Upper && typed.size() > 1) {
        return ascii_upper(entry.display);
    }
    if (form == CaseForm::Lower) {
        if ((entry.accept & kLow) != 0) {
            return ascii_lower(entry.display);
        }
        return entry.display;
    }
    if (form == CaseForm::Capital) {
        if ((entry.accept & kLow) == 0) {
            return entry.display;
        }
        std::string shown = entry.display;
        if (!shown.empty() && shown[0] >= 'a' && shown[0] <= 'z') {
            shown[0] = static_cast<char>(shown[0] - 'a' + 'A');
        }
        return shown;
    }
    return entry.display;
}

namespace {

int common_prefix(std::string_view a, std::string_view b) {
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) {
        ++i;
    }
    return static_cast<int>(i);
}

}  // namespace

std::vector<std::string> Dictionary::suggest_single(const std::string& word, int limit, bool deep) const {
    struct Candidate {
        std::string text;
        int distance = 0;
        int prefix = 0;
        int length_gap = 0;
    };
    std::vector<Candidate> found;
    std::unordered_set<std::string> seen;
    const std::string typed_lower = ascii_lower(word);

    auto consider = [&](const std::string& key, int distance) {
        if (key.empty() || key == typed_lower || seen.count(key) != 0) {
            return;
        }
        const auto it = entries_.find(key);
        if (it == entries_.end() || it->second.nosuggest) {
            return;
        }
        seen.insert(key);
        Candidate candidate;
        candidate.text = present_entry(it->second, word);
        candidate.distance = distance;
        candidate.prefix = common_prefix(typed_lower, key);
        candidate.length_gap = std::abs(static_cast<int>(key.size()) - static_cast<int>(typed_lower.size()));
        found.push_back(std::move(candidate));
    };

    auto consider_phrase = [&](const std::string& phrase, int distance) {
        if (phrase.empty() || seen.count(phrase) != 0) {
            return;
        }
        seen.insert(phrase);
        Candidate candidate;
        candidate.text = phrase;
        if (classify(word) == CaseForm::Capital && !candidate.text.empty() && candidate.text[0] >= 'a' &&
            candidate.text[0] <= 'z') {
            candidate.text[0] = static_cast<char>(candidate.text[0] - 'a' + 'A');
        } else if (classify(word) == CaseForm::Upper && word.size() > 1) {
            candidate.text = ascii_upper(candidate.text);
        }
        candidate.distance = distance;
        candidate.prefix = common_prefix(typed_lower, ascii_lower(phrase));
        candidate.length_gap = std::abs(static_cast<int>(phrase.size()) - static_cast<int>(word.size()));
        found.push_back(std::move(candidate));
    };

    each_edit1(typed_lower, [&](std::string edited) {
        consider(edited, 1);
        return true;
    });

    for (const Rep& rep : reps_) {
        if (rep.from.empty()) {
            continue;
        }
        for (size_t i = 0; i + rep.from.size() <= typed_lower.size(); ++i) {
            if (typed_lower.compare(i, rep.from.size(), rep.from) != 0) {
                continue;
            }
            const std::string candidate = typed_lower.substr(0, i) + rep.to + typed_lower.substr(i + rep.from.size());
            if (candidate.find(' ') != std::string::npos) {
                bool ok = true;
                size_t start = 0;
                int parts = 0;
                while (start <= candidate.size()) {
                    const size_t space = candidate.find(' ', start);
                    const std::string part =
                        candidate.substr(start, space == std::string::npos ? std::string::npos : space - start);
                    if (part.empty() || entries_.find(part) == entries_.end()) {
                        ok = false;
                        break;
                    }
                    ++parts;
                    if (space == std::string::npos) {
                        break;
                    }
                    start = space + 1;
                }
                if (ok && parts >= 2) {
                    consider_phrase(candidate, 1);
                }
            } else {
                consider(candidate, 1);
            }
        }
    }

    if (typed_lower.size() >= 4 && typed_lower.size() <= 24) {
        for (size_t split = 2; split + 2 <= typed_lower.size(); ++split) {
            const std::string left = typed_lower.substr(0, split);
            const std::string right = typed_lower.substr(split);
            if (entries_.count(left) != 0 && entries_.count(right) != 0) {
                consider_phrase(left + " " + right, 1);
            }
        }
    }

    const auto distance1 = static_cast<int>(std::count_if(found.begin(), found.end(), [](const Candidate& item) {
        return item.distance == 1;
    }));
    if (deep && distance1 < 4 && typed_lower.size() <= 16) {
        int probes = 0;
        each_edit1(typed_lower, [&](const std::string& mid) {
            if (found.size() >= 12 || probes > 20000) {
                return false;
            }
            return each_edit1(mid, [&](std::string edited) {
                if (found.size() >= 12 || probes > 20000) {
                    return false;
                }
                ++probes;
                consider(edited, 2);
                return true;
            });
        });
    }

    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
        if (a.distance != b.distance) {
            return a.distance < b.distance;
        }
        if (a.prefix != b.prefix) {
            return a.prefix > b.prefix;
        }
        if (a.length_gap != b.length_gap) {
            return a.length_gap < b.length_gap;
        }
        return a.text < b.text;
    });
    std::vector<std::string> out;
    std::unordered_set<std::string> unique;
    for (const Candidate& candidate : found) {
        if (unique.insert(candidate.text).second) {
            out.push_back(candidate.text);
        }
        if (static_cast<int>(out.size()) >= limit) {
            break;
        }
    }
    return out;
}

std::vector<std::string> Dictionary::suggest(std::string_view word, int limit, bool deep) const {
    if (!ready_ || limit <= 0) {
        return {};
    }
    bool foreign = false;
    const std::string normalized = normalize_word(word, foreign);
    if (foreign || normalized.empty()) {
        return {};
    }
    const std::string cache_key = normalized + (deep ? "|d" : "|s");
    if (const auto it = cache_.find(cache_key); it != cache_.end()) {
        return it->second;
    }
    std::vector<std::string> results = suggest_single(normalized, limit, deep);
    if (normalized.find('-') != std::string::npos) {
        std::vector<std::string> parts;
        size_t start = 0;
        bool usable = true;
        while (start <= normalized.size()) {
            const size_t dash = normalized.find('-', start);
            std::string part = normalized.substr(start, dash == std::string::npos ? std::string::npos : dash - start);
            if (part.empty()) {
                usable = false;
                break;
            }
            if (!check_single(part)) {
                const auto options = suggest_single(part, 1, false);
                if (options.empty()) {
                    usable = false;
                    break;
                }
                part = options.front();
            }
            parts.push_back(std::move(part));
            if (dash == std::string::npos) {
                break;
            }
            start = dash + 1;
        }
        if (usable && parts.size() >= 2) {
            std::string joined = parts[0];
            for (size_t i = 1; i < parts.size(); ++i) {
                joined.push_back('-');
                joined += parts[i];
            }
            results.insert(results.begin(), std::move(joined));
        }
    }
    if (static_cast<int>(results.size()) > limit) {
        results.resize(static_cast<size_t>(limit));
    }
    if (cache_.size() > 400) {
        cache_.clear();
    }
    cache_[cache_key] = results;
    return results;
}

bool Dictionary::learn(const std::string& word, std::string& error) {
    bool foreign = false;
    const std::string normalized = normalize_word(word, foreign);
    if (foreign || normalized.empty() || normalized.size() > 64) {
        error = "That doesn't look like a single English word";
        return false;
    }
    for (unsigned char c : normalized) {
        if (std::isalpha(c) == 0 && c != '\'' && c != '-') {
            error = "That doesn't look like a single English word";
            return false;
        }
    }
    add_form(normalized, false);
    ignored_.erase(ascii_lower(normalized));
    cache_.clear();
    namespace fs = std::filesystem;
    const fs::path path(personal_path_.empty() ? personal_path() : personal_path_);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::app);
    if (!out) {
        error = "Added for this session, but could not save " + path.string();
        return false;
    }
    out << normalized << '\n';
    personal_path_ = path.string();
    return true;
}

void Dictionary::ignore(std::string_view word) {
    bool foreign = false;
    const std::string normalized = normalize_word(word, foreign);
    if (!foreign && !normalized.empty()) {
        ignored_.insert(ascii_lower(normalized));
    }
}

namespace {

int edits_within(const std::string& left, const std::string& right, int limit) {
    const int rows = static_cast<int>(left.size());
    const int columns = static_cast<int>(right.size());
    if (std::abs(rows - columns) > limit || rows > 48 || columns > 48) {
        return limit + 1;
    }
    int earlier[49];
    int previous[49];
    int current[49];
    for (int column = 0; column <= columns; ++column) {
        previous[column] = column;
        earlier[column] = 0;
    }
    for (int row = 1; row <= rows; ++row) {
        current[0] = row;
        int best = current[0];
        for (int column = 1; column <= columns; ++column) {
            const int cost = left[static_cast<size_t>(row - 1)] == right[static_cast<size_t>(column - 1)] ? 0 : 1;
            current[column] = std::min(previous[column] + 1, std::min(current[column - 1] + 1, previous[column - 1] + cost));
            if (row > 1 && column > 1 && left[static_cast<size_t>(row - 1)] == right[static_cast<size_t>(column - 2)] &&
                left[static_cast<size_t>(row - 2)] == right[static_cast<size_t>(column - 1)]) {
                current[column] = std::min(current[column], earlier[column - 2] + 1);
            }
            best = std::min(best, current[column]);
        }
        if (best > limit) {
            return limit + 1;
        }
        for (int column = 0; column <= columns; ++column) {
            earlier[column] = previous[column];
            previous[column] = current[column];
        }
    }
    return previous[columns];
}

bool title_name(const std::string& token) {
    if (token.size() < 3 || token.size() > 40) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(token[0]);
    if (first < 'A' || first > 'Z') {
        return false;
    }
    bool lower = false;
    for (size_t i = 1; i < token.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(token[i]);
        if (c >= 'a' && c <= 'z') {
            lower = true;
        } else if (!((c >= 'A' && c <= 'Z') || c == '\'' || c == '-')) {
            return false;
        }
    }
    return lower;
}

bool close_typo(const Dictionary& dictionary, const std::string& token) {
    const std::string folded = ascii_lower(token);
    for (const std::string& idea : dictionary.suggest(token, 4, false)) {
        const std::string other = ascii_lower(idea);
        if (other != folded && edits_within(folded, other, 1) <= 1) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<Issue> spelling_issues(const std::string& text, const Dictionary& dictionary) {
    std::vector<Issue> issues;
    if (!dictionary.ready()) {
        return issues;
    }
    for (const WordSpan& word : scan_words(text)) {
        const std::string token = text.substr(word.begin, word.end - word.begin);
        if (token.find('@') != std::string::npos || token.find("://") != std::string::npos) {
            continue;
        }
        if (dictionary.check(token)) {
            continue;
        }
        Issue issue;
        issue.begin = word.begin;
        issue.end = word.end;
        if (title_name(token) && !close_typo(dictionary, token)) {
            issue.kind = IssueKind::Name;
            issue.message = "Possible name";
        } else {
            issue.kind = IssueKind::Spelling;
            issue.message = "Unknown word";
        }
        issues.push_back(std::move(issue));
    }
    return issues;
}

}  // namespace inkwell
