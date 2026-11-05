#pragma once

#include <filesystem>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace inkwell {

inline std::filesystem::path executable_path() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(std::move(buffer));
#elif defined(__linux__)
    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        return std::filesystem::path(buffer);
    }
    return {};
#else
    return {};
#endif
}

}  // namespace inkwell
