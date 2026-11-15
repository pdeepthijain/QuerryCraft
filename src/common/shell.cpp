#include "common/shell.hpp"
#include "common/config.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace querycraft::common {

Shell::Shell(std::istream& in, std::ostream& out, std::ostream& err)
    : in_(in), out_(out), err_(err), is_running_(false) {}

Shell::Shell() : Shell(std::cin, std::cout, std::cerr) {}

void Shell::run() {
    is_running_ = true;
    print_welcome();

    std::string line;
    while (is_running_) {
        print_prompt();
        if (!std::getline(in_, line)) {
            // Reached EOF or stream closed
            print_goodbye();
            break;
        }

        if (process_line(line) == ShellAction::Exit) {
            is_running_ = false;
        }
    }
}

ShellAction Shell::process_line(std::string_view raw_line) {
    std::string line = trim(raw_line);

    // Gracefully ignore empty input
    if (line.empty()) {
        return ShellAction::Continue;
    }

    std::string upper_cmd = to_upper(line);

    // Meta commands: Exit / Quit
    if (upper_cmd == "EXIT" || upper_cmd == "QUIT" ||
        upper_cmd == ".EXIT" || upper_cmd == ".QUIT") {
        print_goodbye();
        return ShellAction::Exit;
    }

    // Meta command: Help
    if (upper_cmd == "HELP" || upper_cmd == ".HELP") {
        print_help();
        return ShellAction::Continue;
    }

    // Foundation phase: Display entered command without executing SQL
    out_ << "Entered command: " << line << '\n';
    out_ << "[Note: SQL execution engine is not yet connected (Foundation phase).]\n";

    return ShellAction::Continue;
}

void Shell::print_welcome() const {
    out_ << "======================================================================\n";
    out_ << "  QueryCraft Database Engine (v" << QUERYCRAFT_VERSION << ")\n";
    out_ << "  A Custom Relational Database Engine Built from Scratch in C++20.\n";
    out_ << "  Type 'HELP' for information or 'EXIT' / 'QUIT' to terminate session.\n";
    out_ << "======================================================================\n";
}

void Shell::print_prompt() const {
    out_ << "querycraft> " << std::flush;
}

void Shell::print_goodbye() const {
    out_ << "Exiting QueryCraft. Goodbye!\n";
}

void Shell::print_help() const {
    out_ << "\n--- QueryCraft Interactive Shell Help ---\n";
    out_ << "Available commands:\n";
    out_ << "  HELP, .help          Display this help message.\n";
    out_ << "  EXIT, QUIT           Exit the interactive shell.\n";
    out_ << "  .exit, .quit         Alternative exit command.\n";
    out_ << "\nStatus:\n";
    out_ << "  Current Phase        Task Foundation (Phase 1)\n";
    out_ << "  Storage Engine       Pending (Upcoming Phase)\n";
    out_ << "  Buffer Pool Manager  Pending (Upcoming Phase)\n";
    out_ << "  SQL Parser           Pending (Upcoming Phase)\n";
    out_ << "  Execution Engine     Pending (Upcoming Phase)\n\n";
}

std::string Shell::trim(std::string_view str) {
    const auto first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string_view::npos) {
        return "";
    }
    const auto last = str.find_last_not_of(" \t\n\r\f\v");
    return std::string(str.substr(first, (last - first + 1)));
}

std::string Shell::to_upper(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char ch : str) {
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return result;
}

} // namespace querycraft::common
