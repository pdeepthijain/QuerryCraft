#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace querycraft::common {

enum class ShellAction {
    Continue,
    Exit
};

class Shell {
public:
    explicit Shell(std::istream& in, std::ostream& out, std::ostream& err);
    Shell(); // Defaults to std::cin, std::cout, std::cerr

    ~Shell() = default;

    // Disallow copy; allow move
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;
    Shell(Shell&&) noexcept = default;
    Shell& operator=(Shell&&) noexcept = default;

    // Run the main interactive REPL loop
    void run();

    // Process a single line of input (isolated, pure logic for easy testing)
    ShellAction process_line(std::string_view line);

    // Prompt, banner and message printing helpers
    void print_welcome() const;
    void print_prompt() const;
    void print_goodbye() const;
    void print_help() const;

    // String utilities
    static std::string trim(std::string_view str);
    static std::string to_upper(std::string_view str);

private:
    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    bool is_running_{false};
};

} // namespace querycraft::common
