#pragma once

#include "issues.hpp"

#include <string>
#include <vector>

namespace inkwell {

struct PluginInfo {
    std::string id;
    std::string name;
    std::string path;
};

void reload_plugins();
const std::vector<PluginInfo>& loaded_plugins();
const std::vector<std::string>& plugin_errors();
void append_plugin_issues(const std::string& text, std::vector<Issue>& issues);

}  // namespace inkwell
