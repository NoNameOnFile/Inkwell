#include "lint.hpp"
#include "gui/mainwindow.hpp"

#include <QApplication>
#include <QString>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cerr << "Inkwell is a writing desk with spelling and grammar.\n\n"
                 "  inkwell [file]          Open the window\n"
                 "  inkwell --check <file>  Print notes and exit\n"
                 "  inkwell-tui [file]      Open the terminal desk\n"
                 "  inkwell --help\n";
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
