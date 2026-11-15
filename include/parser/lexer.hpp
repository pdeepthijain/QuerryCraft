#pragma once

#include "common/status.hpp"
#include "parser/token.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace querycraft::parser {

class Lexer {
public:
    explicit Lexer(std::string_view source);

    // Disable copy; enable move
    Lexer(const Lexer&) = delete;
    Lexer& operator=(const Lexer&) = delete;
    Lexer(Lexer&&) noexcept = default;
    Lexer& operator=(Lexer&&) noexcept = default;

    // Scan and return the next token
    Token next_token();

    // Look at the next token without advancing the lexer
    Token peek_token();

    // Tokenize the entire source into a sequence of tokens ending with END_OF_INPUT or INVALID
    std::vector<Token> tokenize();

    // Error status reporting
    [[nodiscard]] bool has_error() const noexcept { return !status_.ok(); }
    [[nodiscard]] const common::Status& status() const noexcept { return status_; }

    // Reset the lexer with a new source string
    void reset(std::string_view source);

    // Current source coordinates
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    [[nodiscard]] std::size_t column() const noexcept { return col_; }

private:
    void skip_whitespace_and_comments();
    Token scan_token();
    Token scan_identifier_or_keyword();
    Token scan_number();
    Token scan_number_with_leading_dot(std::size_t start_line, std::size_t start_col);
    Token scan_string();

    [[nodiscard]] bool is_at_end() const noexcept;
    char advance();
    [[nodiscard]] char peek() const noexcept;
    [[nodiscard]] char peek_next() const noexcept;
    bool match(char expected);

    std::string_view source_;
    std::size_t cursor_{0};
    std::size_t line_{1};
    std::size_t col_{1};

    common::Status status_{common::Status::Ok()};
    std::optional<Token> peeked_token_{std::nullopt};
};

} // namespace querycraft::parser
