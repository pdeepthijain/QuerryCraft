#pragma once

#include "common/status.hpp"
#include "parser/ast.hpp"
#include "parser/lexer.hpp"
#include "parser/token.hpp"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace querycraft::parser {

class Parser {
public:
    explicit Parser(Lexer& lexer);
    explicit Parser(std::string_view sql);

    // Disable copy; enable move
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
    Parser(Parser&&) noexcept = default;
    Parser& operator=(Parser&&) noexcept = default;

    // Parse a single SQL statement
    std::unique_ptr<Statement> parse_statement();

    // Parse multiple semicolon-separated statements until END_OF_INPUT
    std::vector<std::unique_ptr<Statement>> parse_statements();

    // Parse an isolated expression (useful for WHERE clauses or expression testing)
    std::unique_ptr<Expression> parse_expression();

    // Statement parsers
    std::unique_ptr<SelectStatement> parse_select_statement();
    std::unique_ptr<CreateTableStatement> parse_create_table_statement();
    std::unique_ptr<InsertStatement> parse_insert_statement();
    std::unique_ptr<UpdateStatement> parse_update_statement();
    std::unique_ptr<DeleteStatement> parse_delete_statement();
    std::unique_ptr<CreateIndexStatement> parse_create_index_statement();

    // Expression precedence parsers (descending precedence)
    std::unique_ptr<Expression> parse_or_expression();
    std::unique_ptr<Expression> parse_and_expression();
    std::unique_ptr<Expression> parse_not_expression();
    std::unique_ptr<Expression> parse_comparison_expression();
    std::unique_ptr<Expression> parse_additive_expression();
    std::unique_ptr<Expression> parse_multiplicative_expression();
    std::unique_ptr<Expression> parse_unary_expression();
    std::unique_ptr<Expression> parse_primary_expression();

    // Status and diagnostic queries
    [[nodiscard]] bool has_error() const noexcept { return !status_.ok(); }
    [[nodiscard]] const common::Status& status() const noexcept { return status_; }

    // Static one-shot parsing convenience utility
    static std::pair<std::unique_ptr<Statement>, common::Status> parse(std::string_view sql);

private:
    // Token navigation and lookahead
    Token peek();
    Token advance();
    bool check(TokenType expected);
    bool match(TokenType expected);
    Token consume(TokenType expected, std::string_view error_message);
    void set_error(std::string_view message, const Token& token);
    void synchronize();

    Lexer owned_lexer_{""};
    Lexer* lexer_{nullptr};
    Token current_token_{};
    common::Status status_{common::Status::Ok()};
};

} // namespace querycraft::parser
