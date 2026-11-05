#include "plugin.hpp"

#include "hostpath.hpp"
#include "plugin.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace inkwell {
namespace {

#if defined(_WIN32)
using LibraryHandle = HMODULE;
#else
using LibraryHandle = void*;
#endif

struct LoadedPlugin {
    LibraryHandle handle = nullptr;
    const InkwellPlugin* api = nullptr;
    PluginInfo info;
    bool failed = false;
};

std::mutex g_plugins_mu;
bool g_plugins_ready = false;
std::vector<LoadedPlugin> g_plugins;
std::vector<PluginInfo> g_plugin_info;
std::vector<std::string> g_plugin_errors;

void close_library(LibraryHandle handle) {
    if (handle == nullptr) {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(handle);
#else
    ::dlclose(handle);
#endif
}

void close_plugins() {
    for (LoadedPlugin& plugin : g_plugins) {
        close_library(plugin.handle);
        plugin.handle = nullptr;
    }
    g_plugins.clear();
    g_plugin_info.clear();
    g_plugin_errors.clear();
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

#if !defined(_WIN32)
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
#endif

std::vector<std::filesystem::path> plugin_directories() {
    std::vector<std::filesystem::path> directories;
    if (const char* override_dir = std::getenv("INKWELL_PLUGINS")) {
        if (override_dir[0] != '\0') {
            add_directory(directories, override_dir);
        }
        return directories;
    }

#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        if (local[0] != '\0') {
            add_directory(directories, std::filesystem::path(local) / "inkwell" / "plugins");
        }
    } else if (const char* roaming = std::getenv("APPDATA")) {
        if (roaming[0] != '\0') {
            add_directory(directories, std::filesystem::path(roaming) / "inkwell" / "plugins");
        }
    }
#else
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
#endif

    const std::filesystem::path executable = executable_path();
    if (!executable.empty()) {
        const std::filesystem::path bindir = executable.parent_path();
        add_directory(directories, bindir / "plugins");
#if !defined(_WIN32)
        add_directory(directories, bindir.parent_path() / "lib" / "inkwell" / "plugins");
        add_directory(directories, bindir.parent_path() / "lib64" / "inkwell" / "plugins");
#endif
    }
#if !defined(_WIN32)
    add_directory(directories, "/app/lib/inkwell/plugins");
#endif
    return directories;
}

bool is_plugin_file(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
#if defined(_WIN32)
    if (extension.size() != 4) {
        return false;
    }
    const char* suffix = ".dll";
    for (size_t i = 0; i < extension.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(extension[i]);
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        if (c != static_cast<unsigned char>(suffix[i])) {
            return false;
        }
    }
    return true;
#else
    return extension == ".so";
#endif
}

LibraryHandle open_library(const std::filesystem::path& path, std::string& error) {
#if defined(_WIN32)
    LibraryHandle handle = ::LoadLibraryW(path.c_str());
    if (handle == nullptr) {
        error = "error " + std::to_string(::GetLastError());
    }
    return handle;
#else
    LibraryHandle handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* reason = ::dlerror();
        error = reason != nullptr ? reason : "could not open";
    }
    return handle;
#endif
}

InkwellPluginEntry library_entry(LibraryHandle handle) {
#if defined(_WIN32)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4191)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    auto* entry = reinterpret_cast<InkwellPluginEntry>(::GetProcAddress(handle, "inkwell_plugin"));
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return entry;
#else
    ::dlerror();
    auto* entry = reinterpret_cast<InkwellPluginEntry>(::dlsym(handle, "inkwell_plugin"));
    const char* symbol_error = ::dlerror();
    if (symbol_error != nullptr) {
        return nullptr;
    }
    return entry;
#endif
}

void load_one(const std::filesystem::path& path) {
    std::string open_error;
    LibraryHandle handle = open_library(path, open_error);
    if (handle == nullptr) {
        g_plugin_errors.push_back(path.string() + ": " + open_error);
        return;
    }
    InkwellPluginEntry entry = library_entry(handle);
    if (entry == nullptr) {
        g_plugin_errors.push_back(path.string() + ": missing inkwell_plugin");
        close_library(handle);
        return;
    }
    const InkwellPlugin* api = entry();
    if (api == nullptr || api->abi != INKWELL_PLUGIN_ABI || api->analyze == nullptr) {
        g_plugin_errors.push_back(path.string() + ": unsupported plugin");
        close_library(handle);
        return;
    }
    const std::string id = api->id != nullptr && api->id[0] != '\0' ? api->id : path.stem().string();
    for (const LoadedPlugin& existing : g_plugins) {
        if (existing.info.id == id) {
            g_plugin_errors.push_back(path.string() + ": " + id + " is already loaded");
            close_library(handle);
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
            if (!it->is_regular_file(file_error) || !is_plugin_file(it->path())) {
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
