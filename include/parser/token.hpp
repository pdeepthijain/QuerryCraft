#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace querycraft::parser {

enum class TokenType : std::uint8_t {
    // Keywords
    SELECT,
    FROM,
    WHERE,
    CREATE,
    TABLE,
    INSERT,
    INTO,
    VALUES,
    UPDATE,
    SET,
    DELETE,
    DROP,
    INDEX,
    ON,
    AND,
    OR,
    NOT,
    NULL_KEYWORD,
    INT,
    FLOAT,
    TEXT,
    VARCHAR,
    PRIMARY,
    KEY,
    ORDER,
    BY,
    ASC,
    DESC,
    LIMIT,
    JOIN,
    INNER,
    LEFT,
    GROUP,
    HAVING,

    // Identifiers and literals
    IDENTIFIER,
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,

    // Operators
    EQUAL,          // =
    NOT_EQUAL,      // != or <>
    LESS_THAN,      // <
    LESS_EQUAL,     // <=
    GREATER_THAN,   // >
    GREATER_EQUAL,  // >=
    PLUS,           // +
    MINUS,          // -
    MULTIPLY,       // *
    DIVIDE,         // /

    // Punctuation
    LEFT_PAREN,     // (
    RIGHT_PAREN,    // )
    COMMA,          // ,
    DOT,            // .
    SEMICOLON,      // ;

    // Special
    END_OF_INPUT,
    INVALID,

    // Common aliases
    KW_NULL = NULL_KEYWORD,
    KEYWORD_NULL = NULL_KEYWORD,
    NULL_VALUE = NULL_KEYWORD
};

// Returns the textual representation of the TokenType name
constexpr std::string_view token_type_name(TokenType type) noexcept {
    switch (type) {
        // Keywords
        case TokenType::SELECT:        return "SELECT";
        case TokenType::FROM:          return "FROM";
        case TokenType::WHERE:         return "WHERE";
        case TokenType::CREATE:        return "CREATE";
        case TokenType::TABLE:         return "TABLE";
        case TokenType::INSERT:        return "INSERT";
        case TokenType::INTO:          return "INTO";
        case TokenType::VALUES:        return "VALUES";
        case TokenType::UPDATE:        return "UPDATE";
        case TokenType::SET:           return "SET";
        case TokenType::DELETE:        return "DELETE";
        case TokenType::DROP:          return "DROP";
        case TokenType::INDEX:         return "INDEX";
        case TokenType::ON:            return "ON";
        case TokenType::AND:           return "AND";
        case TokenType::OR:            return "OR";
        case TokenType::NOT:           return "NOT";
        case TokenType::NULL_KEYWORD:  return "NULL";
        case TokenType::INT:           return "INT";
        case TokenType::FLOAT:         return "FLOAT";
        case TokenType::TEXT:          return "TEXT";
        case TokenType::VARCHAR:       return "VARCHAR";
        case TokenType::PRIMARY:       return "PRIMARY";
        case TokenType::KEY:           return "KEY";
        case TokenType::ORDER:         return "ORDER";
        case TokenType::BY:            return "BY";
        case TokenType::ASC:           return "ASC";
        case TokenType::DESC:          return "DESC";
        case TokenType::LIMIT:         return "LIMIT";
        case TokenType::JOIN:          return "JOIN";
        case TokenType::INNER:         return "INNER";
        case TokenType::LEFT:          return "LEFT";
        case TokenType::GROUP:         return "GROUP";
        case TokenType::HAVING:        return "HAVING";

        // Identifiers & Literals
        case TokenType::IDENTIFIER:      return "IDENTIFIER";
        case TokenType::INTEGER_LITERAL: return "INTEGER_LITERAL";
        case TokenType::FLOAT_LITERAL:   return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL:  return "STRING_LITERAL";

        // Operators
        case TokenType::EQUAL:          return "EQUAL";
        case TokenType::NOT_EQUAL:      return "NOT_EQUAL";
        case TokenType::LESS_THAN:      return "LESS_THAN";
        case TokenType::LESS_EQUAL:     return "LESS_EQUAL";
        case TokenType::GREATER_THAN:   return "GREATER_THAN";
        case TokenType::GREATER_EQUAL:  return "GREATER_EQUAL";
        case TokenType::PLUS:           return "PLUS";
        case TokenType::MINUS:          return "MINUS";
        case TokenType::MULTIPLY:       return "MULTIPLY";
        case TokenType::DIVIDE:         return "DIVIDE";

        // Punctuation
        case TokenType::LEFT_PAREN:     return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN:    return "RIGHT_PAREN";
        case TokenType::COMMA:          return "COMMA";
        case TokenType::DOT:            return "DOT";
        case TokenType::SEMICOLON:      return "SEMICOLON";

        // Special
        case TokenType::END_OF_INPUT:   return "END_OF_INPUT";
        case TokenType::INVALID:        return "INVALID";
    }
    return "UNKNOWN";
}

constexpr bool is_keyword(TokenType type) noexcept {
    return static_cast<std::uint8_t>(type) <= static_cast<std::uint8_t>(TokenType::HAVING);
}

constexpr bool is_literal(TokenType type) noexcept {
    return type == TokenType::INTEGER_LITERAL ||
           type == TokenType::FLOAT_LITERAL ||
           type == TokenType::STRING_LITERAL;
}

constexpr bool is_operator(TokenType type) noexcept {
    return static_cast<std::uint8_t>(type) >= static_cast<std::uint8_t>(TokenType::EQUAL) &&
           static_cast<std::uint8_t>(type) <= static_cast<std::uint8_t>(TokenType::DIVIDE);
}

constexpr bool is_punctuation(TokenType type) noexcept {
    return static_cast<std::uint8_t>(type) >= static_cast<std::uint8_t>(TokenType::LEFT_PAREN) &&
           static_cast<std::uint8_t>(type) <= static_cast<std::uint8_t>(TokenType::SEMICOLON);
}

struct SourceLocation {
    std::size_t line{1};
    std::size_t column{1};

    constexpr bool operator==(const SourceLocation& other) const noexcept = default;
};

class Token {
public:
    Token() = default;

    Token(TokenType type, std::string lexeme, SourceLocation location)
        : type_(type), lexeme_(std::move(lexeme)), location_(location) {}

    Token(TokenType type, std::string lexeme, std::size_t line, std::size_t column)
        : type_(type), lexeme_(std::move(lexeme)), location_{line, column} {}

    [[nodiscard]] TokenType type() const noexcept { return type_; }
    [[nodiscard]] const std::string& lexeme() const noexcept { return lexeme_; }
    [[nodiscard]] const SourceLocation& location() const noexcept { return location_; }
    [[nodiscard]] std::size_t line() const noexcept { return location_.line; }
    [[nodiscard]] std::size_t column() const noexcept { return location_.column; }

    [[nodiscard]] bool is(TokenType expected_type) const noexcept { return type_ == expected_type; }
    [[nodiscard]] bool is_valid() const noexcept { return type_ != TokenType::INVALID; }
    [[nodiscard]] bool is_end_of_input() const noexcept { return type_ == TokenType::END_OF_INPUT; }
    [[nodiscard]] bool is_keyword() const noexcept { return parser::is_keyword(type_); }
    [[nodiscard]] bool is_literal() const noexcept { return parser::is_literal(type_); }
    [[nodiscard]] bool is_operator() const noexcept { return parser::is_operator(type_); }
    [[nodiscard]] bool is_punctuation() const noexcept { return parser::is_punctuation(type_); }

    // Human-readable format matching expected sequence:
    // e.g. KEYWORD(SELECT), IDENTIFIER(name), COMMA, FLOAT_LITERAL(9.0), END_OF_INPUT
    [[nodiscard]] std::string format() const {
        if (is_keyword()) {
            return "KEYWORD(" + std::string(token_type_name(type_)) + ")";
        }
        if (type_ == TokenType::IDENTIFIER) {
            return "IDENTIFIER(" + lexeme_ + ")";
        }
        if (type_ == TokenType::INTEGER_LITERAL) {
            return "INTEGER_LITERAL(" + lexeme_ + ")";
        }
        if (type_ == TokenType::FLOAT_LITERAL) {
            return "FLOAT_LITERAL(" + lexeme_ + ")";
        }
        if (type_ == TokenType::STRING_LITERAL) {
            return "STRING_LITERAL(" + lexeme_ + ")";
        }
        if (type_ == TokenType::INVALID) {
            return "INVALID(" + lexeme_ + ")";
        }
        return std::string(token_type_name(type_));
    }

    [[nodiscard]] std::string to_string() const {
        return format();
    }

    bool operator==(const Token& other) const noexcept {
        return type_ == other.type_ && lexeme_ == other.lexeme_;
    }

    bool operator!=(const Token& other) const noexcept {
        return !(*this == other);
    }

private:
    TokenType type_{TokenType::INVALID};
    std::string lexeme_{};
    SourceLocation location_{1, 1};
};

inline std::ostream& operator<<(std::ostream& os, TokenType type) {
    os << token_type_name(type);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Token& token) {
    os << token.format();
    return os;
}

} // namespace querycraft::parser
