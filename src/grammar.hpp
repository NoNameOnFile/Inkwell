#pragma once

#include "issues.hpp"
#include "spell.hpp"

#include <string>
#include <vector>

namespace inkwell {

std::vector<Issue> grammar_issues(const std::string& text);
std::vector<Issue> analyze_document(const std::string& text, const Dictionary& dictionary);

}  // namespace inkwell
