#pragma once

#include <filesystem>

#include <cstdlib>

inline void set_env(const char* name, const std::filesystem::path& value) {
#if defined(_WIN32)
    _putenv_s(name, value.string().c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}
