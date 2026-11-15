#include "parser/lexer.hpp"

#include <cctype>
#include <string>
#include <unordered_map>

namespace querycraft::parser {

namespace {

const std::unordered_map<std::string_view, TokenType>& get_keywords_map() {
    static const std::unordered_map<std::string_view, TokenType> keywords = {
        {"SELECT",   TokenType::SELECT},
        {"FROM",     TokenType::FROM},
        {"WHERE",    TokenType::WHERE},
        {"CREATE",   TokenType::CREATE},
        {"TABLE",    TokenType::TABLE},
        {"INSERT",   TokenType::INSERT},
        {"INTO",     TokenType::INTO},
        {"VALUES",   TokenType::VALUES},
        {"UPDATE",   TokenType::UPDATE},
        {"SET",      TokenType::SET},
        {"DELETE",   TokenType::DELETE},
        {"DROP",     TokenType::DROP},
        {"INDEX",    TokenType::INDEX},
        {"ON",       TokenType::ON},
        {"AND",      TokenType::AND},
        {"OR",       TokenType::OR},
        {"NOT",      TokenType::NOT},
        {"NULL",     TokenType::NULL_KEYWORD},
        {"INT",      TokenType::INT},
        {"FLOAT",    TokenType::FLOAT},
        {"TEXT",     TokenType::TEXT},
        {"VARCHAR",  TokenType::VARCHAR},
        {"PRIMARY",  TokenType::PRIMARY},
        {"KEY",      TokenType::KEY},
        {"ORDER",    TokenType::ORDER},
        {"BY",       TokenType::BY},
        {"ASC",      TokenType::ASC},
        {"DESC",     TokenType::DESC},
        {"LIMIT",    TokenType::LIMIT},
        {"JOIN",     TokenType::JOIN},
        {"INNER",    TokenType::INNER},
        {"LEFT",     TokenType::LEFT},
        {"GROUP",    TokenType::GROUP},
        {"HAVING",   TokenType::HAVING}
    };
    return keywords;
}

std::string to_upper_copy(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return result;
}

} // namespace

Lexer::Lexer(std::string_view source)
    : source_(source), cursor_(0), line_(1), col_(1), status_(common::Status::Ok()), peeked_token_(std::nullopt) {}

void Lexer::reset(std::string_view source) {
    source_ = source;
    cursor_ = 0;
    line_ = 1;
    col_ = 1;
    status_ = common::Status::Ok();
    peeked_token_ = std::nullopt;
}

Token Lexer::next_token() {
    if (peeked_token_.has_value()) {
        Token token = std::move(*peeked_token_);
        peeked_token_ = std::nullopt;
        return token;
    }
    return scan_token();
}

Token Lexer::peek_token() {
    if (!peeked_token_.has_value()) {
        peeked_token_ = scan_token();
    }
    return *peeked_token_;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token t = next_token();
        tokens.push_back(t);
        if (t.is(TokenType::END_OF_INPUT)) {
            break;
        }
    }
    return tokens;
}

bool Lexer::is_at_end() const noexcept {
    return cursor_ >= source_.size();
}

char Lexer::peek() const noexcept {
    if (is_at_end()) {
        return '\0';
    }
    return source_[cursor_];
}

char Lexer::peek_next() const noexcept {
    if (cursor_ + 1 >= source_.size()) {
        return '\0';
    }
    return source_[cursor_ + 1];
}

char Lexer::advance() {
    char c = source_[cursor_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (is_at_end() || peek() != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::skip_whitespace_and_comments() {
    while (!is_at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            advance();
        } else if (c == '\n') {
            advance();
        } else if (c == '-' && peek_next() == '-') {
            // SQL single-line comment: skip until newline or EOF
            advance(); // first '-'
            advance(); // second '-'
            while (!is_at_end() && peek() != '\n') {
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::scan_identifier_or_keyword() {
    const std::size_t start_pos = cursor_;
    const std::size_t start_line = line_;
    const std::size_t start_col = col_;

    while (!is_at_end()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            advance();
        } else {
            break;
        }
    }

    std::string_view lexeme_view = source_.substr(start_pos, cursor_ - start_pos);
    std::string upper_name = to_upper_copy(lexeme_view);

    const auto& keywords = get_keywords_map();
    auto it = keywords.find(upper_name);
    if (it != keywords.end()) {
        return Token(it->second, std::string(lexeme_view), start_line, start_col);
    }

    return Token(TokenType::IDENTIFIER, std::string(lexeme_view), start_line, start_col);
}

Token Lexer::scan_number() {
    const std::size_t start_pos = cursor_;
    const std::size_t start_line = line_;
    const std::size_t start_col = col_;

    while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    bool is_float = false;

    // Fractional part
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
        is_float = true;
        advance(); // consume '.'
        while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    // Exponent part e.g. 1e5, 1.2e-3
    if (peek() == 'e' || peek() == 'E') {
        char next = peek_next();
        if (std::isdigit(static_cast<unsigned char>(next)) ||
            ((next == '+' || next == '-') && cursor_ + 2 < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[cursor_ + 2])))) {
            is_float = true;
            advance(); // consume 'e' or 'E'
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
    }

    // Check for malformed number with multiple dots: e.g. 9.0.0
    if (peek() == '.') {
        advance();
        while (!is_at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '.' || peek() == '_')) {
            advance();
        }
        std::string lexeme = std::string(source_.substr(start_pos, cursor_ - start_pos));
        status_ = common::Status::SyntaxError("Malformed numeric literal '" + lexeme + "' with multiple decimal points at line " +
                                             std::to_string(start_line) + ", col " + std::to_string(start_col));
        return Token(TokenType::INVALID, lexeme, start_line, start_col);
    }

    // Check for trailing alphabetic characters (e.g. 123abc)
    if (!is_at_end() && (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_')) {
        while (!is_at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            advance();
        }
        std::string lexeme = std::string(source_.substr(start_pos, cursor_ - start_pos));
        status_ = common::Status::SyntaxError("Malformed numeric literal '" + lexeme + "' followed by letters at line " +
                                             std::to_string(start_line) + ", col " + std::to_string(start_col));
        return Token(TokenType::INVALID, lexeme, start_line, start_col);
    }

    std::string lexeme = std::string(source_.substr(start_pos, cursor_ - start_pos));
    if (is_float) {
        return Token(TokenType::FLOAT_LITERAL, lexeme, start_line, start_col);
    }
    return Token(TokenType::INTEGER_LITERAL, lexeme, start_line, start_col);
}

Token Lexer::scan_number_with_leading_dot(std::size_t start_line, std::size_t start_col) {
    const std::size_t start_pos = cursor_ - 1; // '.' was consumed

    while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    // Exponent part e.g. .5e2
    if (peek() == 'e' || peek() == 'E') {
        char next = peek_next();
        if (std::isdigit(static_cast<unsigned char>(next)) ||
            ((next == '+' || next == '-') && cursor_ + 2 < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[cursor_ + 2])))) {
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
    }

    if (!is_at_end() && (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_')) {
        while (!is_at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            advance();
        }
        std::string lexeme = std::string(source_.substr(start_pos, cursor_ - start_pos));
        status_ = common::Status::SyntaxError("Malformed floating point literal '" + lexeme + "' at line " +
                                             std::to_string(start_line) + ", col " + std::to_string(start_col));
        return Token(TokenType::INVALID, lexeme, start_line, start_col);
    }

    std::string lexeme = std::string(source_.substr(start_pos, cursor_ - start_pos));
    return Token(TokenType::FLOAT_LITERAL, lexeme, start_line, start_col);
}

Token Lexer::scan_string() {
    const std::size_t start_pos = cursor_ - 1; // '\'' was already consumed
    const std::size_t start_line = line_;
    const std::size_t start_col = col_ - 1;

    bool terminated = false;
    while (!is_at_end()) {
        char c = peek();
        if (c == '\'') {
            advance();
            if (peek() == '\'') {
                // Escaped single quote: ''
                advance();
            } else {
                terminated = true;
                break;
            }
        } else {
            advance();
        }
    }

    if (!terminated) {
        std::string lexeme = std::string(source_.substr(start_pos, cursor_ - start_pos));
        status_ = common::Status::SyntaxError("Unterminated string literal starting at line " +
                                             std::to_string(start_line) + ", col " + std::to_string(start_col));
        return Token(TokenType::INVALID, lexeme, start_line, start_col);
    }

    std::string lexeme = std::string(source_.substr(start_pos, cursor_ - start_pos));
    return Token(TokenType::STRING_LITERAL, lexeme, start_line, start_col);
}

Token Lexer::scan_token() {
    skip_whitespace_and_comments();

    if (is_at_end()) {
        return Token(TokenType::END_OF_INPUT, "", line_, col_);
    }

    const std::size_t start_line = line_;
    const std::size_t start_col = col_;
    char c = advance();

    // Identifiers or keywords
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        cursor_--;
        col_--;
        return scan_identifier_or_keyword();
    }

    // Numeric literals
    if (std::isdigit(static_cast<unsigned char>(c))) {
        cursor_--;
        col_--;
        return scan_number();
    }

    // Punctuation and Operators
    switch (c) {
        case '(': return Token(TokenType::LEFT_PAREN, "(", start_line, start_col);
        case ')': return Token(TokenType::RIGHT_PAREN, ")", start_line, start_col);
        case ',': return Token(TokenType::COMMA, ",", start_line, start_col);
        case ';': return Token(TokenType::SEMICOLON, ";", start_line, start_col);
        case '+': return Token(TokenType::PLUS, "+", start_line, start_col);
        case '-': return Token(TokenType::MINUS, "-", start_line, start_col);
        case '*': return Token(TokenType::MULTIPLY, "*", start_line, start_col);
        case '/': return Token(TokenType::DIVIDE, "/", start_line, start_col);
        case '=': return Token(TokenType::EQUAL, "=", start_line, start_col);
        case '.':
            if (std::isdigit(static_cast<unsigned char>(peek()))) {
                return scan_number_with_leading_dot(start_line, start_col);
            }
            return Token(TokenType::DOT, ".", start_line, start_col);
        case '<':
            if (match('=')) {
                return Token(TokenType::LESS_EQUAL, "<=", start_line, start_col);
            }
            if (match('>')) {
                return Token(TokenType::NOT_EQUAL, "<>", start_line, start_col);
            }
            return Token(TokenType::LESS_THAN, "<", start_line, start_col);
        case '>':
            if (match('=')) {
                return Token(TokenType::GREATER_EQUAL, ">=", start_line, start_col);
            }
            return Token(TokenType::GREATER_THAN, ">", start_line, start_col);
        case '!':
            if (match('=')) {
                return Token(TokenType::NOT_EQUAL, "!=", start_line, start_col);
            }
            status_ = common::Status::SyntaxError("Unexpected character '!' at line " +
                                                 std::to_string(start_line) + ", col " + std::to_string(start_col));
            return Token(TokenType::INVALID, "!", start_line, start_col);
        case '\'':
            return scan_string();
        default: {
            std::string invalid_char(1, c);
            status_ = common::Status::SyntaxError("Unexpected character '" + invalid_char + "' at line " +
                                                 std::to_string(start_line) + ", col " + std::to_string(start_col));
            return Token(TokenType::INVALID, invalid_char, start_line, start_col);
        }
    }
}

} // namespace querycraft::parser
