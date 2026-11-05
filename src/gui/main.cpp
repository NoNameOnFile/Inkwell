#include "lint.hpp"
#include "gui/mainwindow.hpp"

#include <QApplication>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

void print_usage() {
    std::cerr << "Inkwell is a writing desk with spelling and grammar.\n\n"
                 "  inkwell [file]          Open the window\n"
                 "  inkwell --check <file>  Print notes and exit\n"
                 "  inkwell-tui [file]      Open the terminal desk\n"
                 "  inkwell --help\n";
}

void attach_parent_console() {
#if defined(_WIN32)
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != nullptr && output != INVALID_HANDLE_VALUE) {
        const DWORD kind = ::GetFileType(output);
        if (kind == FILE_TYPE_DISK || kind == FILE_TYPE_PIPE || kind == FILE_TYPE_CHAR) {
            return;
        }
    }
    if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    FILE* stream = nullptr;
#if defined(_MSC_VER)
    if (freopen_s(&stream, "CONOUT$", "w", stdout) == 0) {
        std::cout.clear();
    }
    if (freopen_s(&stream, "CONOUT$", "w", stderr) == 0) {
        std::cerr.clear();
    }
#else
    if (std::freopen("CONOUT$", "w", stdout) != nullptr) {
        std::cout.clear();
    }
    if (std::freopen("CONOUT$", "w", stderr) != nullptr) {
        std::cerr.clear();
    }
#endif
#endif
}

bool display_available() {
#if defined(__linux__)
    const char* platform = std::getenv("QT_QPA_PLATFORM");
    if (platform != nullptr && platform[0] != '\0') {
        return true;
    }
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const bool has_display = display != nullptr && display[0] != '\0';
    const bool has_wayland = wayland != nullptr && wayland[0] != '\0';
    return has_display || has_wayland;
#else
    return true;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    attach_parent_console();
    std::vector<std::string> args;
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
        if (arg == "--version") {
            std::cout << "Inkwell 0.1.0\n";
            return 0;
        }
        if (arg == "--check") {
            check = true;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option " << arg << '\n';
            print_usage();
            return 2;
        }
        args.push_back(arg);
    }

    if (check) {
        return inkwell::lint_paths(args);
    }
    if (args.size() > 1) {
        print_usage();
        return 2;
    }
    if (!display_available()) {
        std::cerr << "No display is available. Run inkwell-tui, or use inkwell --check <file>.\n";
        return 2;
    }

    QApplication app(argc, argv);
    app.setApplicationName("Inkwell");
    inkwell::MainWindow window;
    window.show();
    const bool recovered = window.offerRecovery();
    if (!recovered && !args.empty() && !window.openFile(QString::fromLocal8Bit(args.front().c_str()))) {
        return 2;
    }
    return app.exec();
}
