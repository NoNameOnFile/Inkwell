#include "app.hpp"
#include "lint.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cerr << "Inkwell is a writing desk with spelling and grammar.\n\n"
                 "  inkwell-tui [file]          Open the terminal desk\n"
                 "  inkwell-tui --check <file>  Print notes and exit\n"
                 "  inkwell [file]              Open the window\n"
                 "  inkwell-tui --help\n";
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
    return inkwell::run_app(args.empty() ? std::string{} : args.front());
}
