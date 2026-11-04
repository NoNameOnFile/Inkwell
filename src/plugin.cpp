#include "plugin.hpp"

#include "plugin.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace inkwell {
namespace {

struct LoadedPlugin {
    void* handle = nullptr;
    const InkwellPlugin* api = nullptr;
    PluginInfo info;
    bool failed = false;
};

std::mutex g_plugins_mu;
bool g_plugins_ready = false;
std::vector<LoadedPlugin> g_plugins;
std::vector<PluginInfo> g_plugin_info;
std::vector<std::string> g_plugin_errors;

void close_plugins() {
    for (LoadedPlugin& plugin : g_plugins) {
        if (plugin.handle != nullptr) {
            dlclose(plugin.handle);
            plugin.handle = nullptr;
        }
    }
    g_plugins.clear();
    g_plugin_info.clear();
    g_plugin_errors.clear();
}

std::filesystem::path executable_path() {
#if defined(__linux__)
    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        return std::filesystem::path(buffer);
    }
#endif
    return {};
}

void add_directory(std::vector<std::filesystem::path>& directories, const std::filesystem::path& directory) {
    if (directory.empty()) {
        return;
    }
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(directory, error);
    const std::filesystem::path chosen = error ? directory.lexically_normal() : canonical;
    for (const std::filesystem::path& existing : directories) {
        if (existing == chosen) {
            return;
        }
    }
    directories.push_back(chosen);
}

void add_data_dirs(std::vector<std::filesystem::path>& directories, const std::string& value) {
    std::string rest = value;
    while (!rest.empty()) {
        const std::size_t colon = rest.find(':');
        const std::string dir = rest.substr(0, colon);
        if (!dir.empty()) {
            add_directory(directories, std::filesystem::path(dir) / "inkwell" / "plugins");
        }
        if (colon == std::string::npos) {
            break;
        }
        rest.erase(0, colon + 1);
    }
}

std::vector<std::filesystem::path> plugin_directories() {
    std::vector<std::filesystem::path> directories;
    if (const char* override_dir = std::getenv("INKWELL_PLUGINS")) {
        if (override_dir[0] != '\0') {
            add_directory(directories, override_dir);
        }
        return directories;
    }

    const char* data_home = std::getenv("XDG_DATA_HOME");
    if (data_home != nullptr && data_home[0] != '\0') {
        add_directory(directories, std::filesystem::path(data_home) / "inkwell" / "plugins");
    } else if (const char* home = std::getenv("HOME")) {
        add_directory(directories, std::filesystem::path(home) / ".local" / "share" / "inkwell" / "plugins");
    }

    if (const char* data_dirs = std::getenv("XDG_DATA_DIRS")) {
        add_data_dirs(directories, data_dirs);
    } else {
        add_directory(directories, "/usr/local/share/inkwell/plugins");
        add_directory(directories, "/usr/share/inkwell/plugins");
    }

    const std::filesystem::path executable = executable_path();
    if (!executable.empty()) {
        const std::filesystem::path bindir = executable.parent_path();
        add_directory(directories, bindir / "plugins");
        add_directory(directories, bindir.parent_path() / "lib" / "inkwell" / "plugins");
        add_directory(directories, bindir.parent_path() / "lib64" / "inkwell" / "plugins");
    }
    add_directory(directories, "/app/lib/inkwell/plugins");
    return directories;
}

void load_one(const std::filesystem::path& path) {
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* reason = dlerror();
        g_plugin_errors.push_back(path.string() + ": " + (reason != nullptr ? reason : "could not open"));
        return;
    }
    dlerror();
    auto* entry = reinterpret_cast<InkwellPluginEntry>(dlsym(handle, "inkwell_plugin"));
    const char* symbol_error = dlerror();
    if (symbol_error != nullptr || entry == nullptr) {
        g_plugin_errors.push_back(path.string() + ": missing inkwell_plugin");
        dlclose(handle);
        return;
    }
    const InkwellPlugin* api = entry();
    if (api == nullptr || api->abi != INKWELL_PLUGIN_ABI || api->analyze == nullptr) {
        g_plugin_errors.push_back(path.string() + ": unsupported plugin");
        dlclose(handle);
        return;
    }
    const std::string id = api->id != nullptr && api->id[0] != '\0' ? api->id : path.stem().string();
    for (const LoadedPlugin& existing : g_plugins) {
        if (existing.info.id == id) {
            g_plugin_errors.push_back(path.string() + ": " + id + " is already loaded");
            dlclose(handle);
            return;
        }
    }
    LoadedPlugin loaded;
    loaded.handle = handle;
    loaded.api = api;
    loaded.info.id = id;
    loaded.info.name = api->name != nullptr && api->name[0] != '\0' ? api->name : id;
    loaded.info.path = path.string();
    g_plugin_info.push_back(loaded.info);
    g_plugins.push_back(std::move(loaded));
}

void scan_plugins() {
    close_plugins();
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::path& directory : plugin_directories()) {
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) {
            continue;
        }
        for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
            std::error_code file_error;
            if (!it->is_regular_file(file_error) || it->path().extension() != ".so") {
                continue;
            }
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files) {
        load_one(file);
    }
    g_plugins_ready = true;
}

void ensure_plugins() {
    if (!g_plugins_ready) {
        scan_plugins();
    }
}

struct EmitState {
    const std::string* text = nullptr;
    std::vector<Issue>* issues = nullptr;
};

void emit_note(void* user, const InkwellPluginNote* note) {
    auto* state = static_cast<EmitState*>(user);
    if (state == nullptr || state->text == nullptr || state->issues == nullptr || note == nullptr) {
        return;
    }
    if (note->end < note->begin || note->end > state->text->size()) {
        return;
    }
    Issue issue;
    issue.begin = note->begin;
    issue.end = note->end;
    switch (note->kind) {
        case INKWELL_NOTE_SPELLING:
            issue.kind = IssueKind::Spelling;
            break;
        case INKWELL_NOTE_GRAMMAR:
            issue.kind = IssueKind::Grammar;
            break;
        case INKWELL_NOTE_NAME:
            issue.kind = IssueKind::Name;
            break;
        default:
            issue.kind = IssueKind::Style;
            break;
    }
    if (note->message != nullptr) {
        issue.message = note->message;
    }
    if (note->replacements != nullptr) {
        for (size_t i = 0; i < note->replacement_count; ++i) {
            if (note->replacements[i] != nullptr) {
                issue.replacements.emplace_back(note->replacements[i]);
            }
        }
    }
    state->issues->push_back(std::move(issue));
}

}  // namespace

void reload_plugins() {
    std::lock_guard<std::mutex> lock(g_plugins_mu);
    g_plugins_ready = false;
    scan_plugins();
}

const std::vector<PluginInfo>& loaded_plugins() {
    std::lock_guard<std::mutex> lock(g_plugins_mu);
    ensure_plugins();
    return g_plugin_info;
}

const std::vector<std::string>& plugin_errors() {
    std::lock_guard<std::mutex> lock(g_plugins_mu);
    ensure_plugins();
    return g_plugin_errors;
}

void append_plugin_issues(const std::string& text, std::vector<Issue>& issues) {
    std::lock_guard<std::mutex> lock(g_plugins_mu);
    ensure_plugins();
    EmitState state;
    state.text = &text;
    state.issues = &issues;
    for (LoadedPlugin& plugin : g_plugins) {
        if (plugin.api == nullptr || plugin.api->analyze == nullptr) {
            continue;
        }
        try {
            plugin.api->analyze(text.data(), text.size(), &state, emit_note);
        } catch (...) {
            if (!plugin.failed) {
                plugin.failed = true;
                g_plugin_errors.push_back(plugin.info.name + " stopped on this page");
            }
        }
    }
}

}  // namespace inkwell
