#include "editor.hpp"
#include "grammar.hpp"
#include "layout.hpp"
#include "markup.hpp"
#include "spell.hpp"
#include "text.hpp"
#include "utf8.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL " << line << ": " << expression << '\n';
        ++g_failures;
    }
}

#define CHECK(condition) check(static_cast<bool>(condition), #condition, __LINE__)

void test_utf8_and_wrap() {
    CHECK(inkwell::string_width("hello") == 5);
    CHECK(inkwell::utf8_count("café") == 4);
    CHECK(inkwell::clip_to_width("abcdef", 3) == "abc");
    const auto rows = inkwell::wrap_document({"one two three four"}, 8);
    CHECK(rows.size() >= 2);
    CHECK(rows.front().line == 0);
    CHECK(rows.front().end > rows.front().begin);
}

void test_editor() {
    inkwell::Editor editor;
    editor.insert("Hello");
    editor.newline();
    editor.insert("world");
    CHECK(editor.text() == "Hello\nworld");
    editor.undo();
    CHECK(editor.text() == "Hello\n");
    editor.undo();
    CHECK(editor.text() == "Hello");
    editor.redo();
    CHECK(editor.text() == "Hello\n");
    editor.insert("!");
    CHECK(editor.text() == "Hello\n!");

    editor.toggle_mark();
    editor.move_home();
    CHECK(editor.has_selection());
    editor.insert("Y");
    CHECK(editor.selection_text().empty());

    const auto path = std::filesystem::temp_directory_path() / "inkwell-roundtrip.txt";
    std::string error;
    inkwell::Editor saving;
    saving.insert("alpha");
    saving.newline();
    saving.newline();
    saving.insert("beta");
    CHECK(saving.save_file(path.string(), error));
    inkwell::Editor loaded;
    CHECK(loaded.load_file(path.string(), error));
    CHECK(loaded.text() == "alpha\n\nbeta");
    std::filesystem::remove(path);
}

bool has_replacement(const std::vector<inkwell::Issue>& issues, std::string_view replacement) {
    for (const inkwell::Issue& issue : issues) {
        for (const std::string& item : issue.replacements) {
            if (item == replacement || item.find(replacement) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

void test_grammar() {
    const auto could = inkwell::grammar_issues("I could of gone home.");
    CHECK(has_replacement(could, "could have"));
    const auto article = inkwell::grammar_issues("She ate a apple.");
    CHECK(has_replacement(article, "an"));
    const auto repeated = inkwell::grammar_issues("the the cat sat.");
    CHECK(has_replacement(repeated, "the"));
    const auto contraction = inkwell::grammar_issues("its a fine day and it's own reward.");
    CHECK(has_replacement(contraction, "it's"));
    CHECK(has_replacement(contraction, "its"));
    const auto verb = inkwell::grammar_issues("They was late and he don't know.");
    CHECK(has_replacement(verb, "They were"));
    CHECK(has_replacement(verb, "doesn't"));
    const auto pronoun = inkwell::grammar_issues("i think i'm ready.");
    CHECK(has_replacement(pronoun, "I"));
    CHECK(has_replacement(pronoun, "I'm"));
    const auto clean = inkwell::grammar_issues("I think I'm ready. The cat sat.");
    CHECK(clean.empty());
}

void test_dictionary() {
    inkwell::Dictionary dictionary;
    std::string error;
    CHECK(dictionary.load(error));
    if (!dictionary.ready()) {
        std::cerr << error << '\n';
        return;
    }
    const char* good[] = {"the",     "running", "happier",    "happiness", "unhappy", "unhappiness",
                          "apples",  "writing", "rewrite",    "received",  "receiving","receivable",
                          "London",  "London's","it's",       "its",       "don't",   "I'm",
                          "recreating", "Monday", "separate", "definitely"};
    for (const char* word : good) {
        if (!dictionary.check(word)) {
            std::cerr << "expected in dictionary: " << word << '\n';
            ++g_failures;
        }
    }
    CHECK(!dictionary.check("london"));
    CHECK(!dictionary.check("recieve"));
    CHECK(!dictionary.check("seperate"));
    const auto receive = dictionary.suggest("recieve", 6, false);
    bool found = false;
    for (const std::string& item : receive) {
        if (item == "receive") {
            found = true;
        }
    }
    if (!found) {
        std::cerr << "suggestions for recieve:";
        for (const std::string& item : receive) {
            std::cerr << ' ' << item;
        }
        std::cerr << '\n';
        ++g_failures;
    }
    const std::string sample = "I recieve a apple and I could of gone.";
    const auto issues = inkwell::analyze_document(sample, dictionary);
    CHECK(!issues.empty());
    bool spelling = false;
    bool grammar = false;
    for (const inkwell::Issue& issue : issues) {
        if (issue.kind == inkwell::IssueKind::Spelling) {
            spelling = true;
        }
        if (issue.kind == inkwell::IssueKind::Grammar) {
            grammar = true;
        }
    }
    CHECK(spelling);
    CHECK(grammar);
    const std::string invented_text = "Xytherion drew the blade.";
    const auto invented = inkwell::analyze_document(invented_text, dictionary);
    bool possible_name = false;
    bool invented_spelling = false;
    for (const inkwell::Issue& issue : invented) {
        const std::string word = invented_text.substr(issue.begin, issue.end - issue.begin);
        if (word == "Xytherion" && issue.kind == inkwell::IssueKind::Name) {
            possible_name = true;
        }
        if (word == "Xytherion" && issue.kind == inkwell::IssueKind::Spelling) {
            invented_spelling = true;
        }
    }
    CHECK(possible_name);
    CHECK(!invented_spelling);
    const std::string capitalized_text = "Recieve the letter.";
    const auto capitalized = inkwell::analyze_document(capitalized_text, dictionary);
    bool typo = false;
    for (const inkwell::Issue& issue : capitalized) {
        const std::string word = capitalized_text.substr(issue.begin, issue.end - issue.begin);
        if (word == "Recieve" && issue.kind == inkwell::IssueKind::Spelling) {
            typo = true;
        }
    }
    CHECK(typo);
}

void test_words() {
    const auto word = inkwell::word_touching("say recieve now", 6);
    CHECK(word.has_value());
    CHECK(word->begin == 4);
    CHECK(word->end == 11);
    const auto edge = inkwell::word_touching("say recieve now", 11);
    CHECK(edge.has_value());
    CHECK(edge->begin == 4);
}

void test_markup() {
    const auto plain = inkwell::parse_markup("I recieve a apple.");
    CHECK(plain.size() == 1);
    CHECK(plain[0].kind == inkwell::MarkupBlock::Kind::Paragraph);
    CHECK(plain[0].runs.size() == 1);
    CHECK(plain[0].runs[0].text == "I recieve a apple.");

    const std::string sample = "<p>She <i>waited</i>.</p>\n<hr>\n<field name=\"pov\">Emiri</field>\n<img src=\"tower.png\" alt=\"Tower\">\n";
    const auto parsed = inkwell::parse_markup(sample);
    CHECK(parsed.size() == 4);
    CHECK(parsed[1].kind == inkwell::MarkupBlock::Kind::Scene);
    CHECK(parsed[2].kind == inkwell::MarkupBlock::Kind::Field);
    CHECK(parsed[2].name == "pov");
    CHECK(parsed[2].runs.size() == 1);
    CHECK(parsed[2].runs[0].text == "Emiri");
    CHECK(parsed[3].src == "tower.png");
    CHECK(parsed[0].runs.size() == 3);
    CHECK(parsed[0].runs[1].italic);
    CHECK(parsed[0].runs[1].text == "waited");
    const std::string again = inkwell::emit_markup(parsed);
    const auto round = inkwell::parse_markup(again);
    CHECK(inkwell::emit_markup(round) == again);
    CHECK(inkwell::markup_plain(sample).find("She waited.") != std::string::npos);
    CHECK(inkwell::parse_markup("<p>a &lt; b</p>")[0].runs[0].text == "a < b");
    CHECK(inkwell::emit_markup(inkwell::parse_markup("One\n\nTwo")) == "One\n\nTwo");
    CHECK(inkwell::emit_markup(inkwell::parse_markup("<p>One</p>\n<p></p>\n<p>Two</p>\n")) == "One\n\nTwo");
    CHECK(inkwell::emit_markup(inkwell::parse_markup("* * *\nAfter")) == "* * *\nAfter");
    CHECK(inkwell::emit_markup(inkwell::parse_markup("She <i>waited</i>.\nNext")) == "She <i>waited</i>.\nNext");
    CHECK(inkwell::emit_markup(inkwell::parse_markup("<p>Hello</p>\n<p><i>Wait</i></p>\n")) == "Hello\n<i>Wait</i>");
}

}  // namespace

int main() {
    std::setlocale(LC_ALL, "");
    test_utf8_and_wrap();
    test_editor();
    test_grammar();
    test_dictionary();
    test_words();
    test_markup();
    if (g_failures != 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
