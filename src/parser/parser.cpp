#include "parser/parser.hpp"

#include <string>

namespace querycraft::parser {

namespace {

std::string unquote_sql_string(std::string_view raw) {
    if (raw.size() >= 2 && raw.front() == '\'' && raw.back() == '\'') {
        raw.remove_prefix(1);
        raw.remove_suffix(1);
    }
    std::string result;
    result.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\'' && i + 1 < raw.size() && raw[i + 1] == '\'') {
            result.push_back('\'');
            ++i; // skip second quote in ''
        } else {
            result.push_back(raw[i]);
        }
    }
    return result;
}

} // namespace

Parser::Parser(Lexer& lexer) : lexer_(&lexer) {
    current_token_ = lexer_->next_token();
}

Parser::Parser(std::string_view sql)
    : owned_lexer_(sql), lexer_(&owned_lexer_) {
    current_token_ = lexer_->next_token();
}

Token Parser::peek() {
    return current_token_;
}

Token Parser::advance() {
    Token prev = current_token_;
    if (!current_token_.is(TokenType::END_OF_INPUT)) {
        current_token_ = lexer_->next_token();
    }
    return prev;
}

bool Parser::check(TokenType expected) {
    return current_token_.is(expected);
}

bool Parser::match(TokenType expected) {
    if (check(expected)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::consume(TokenType expected, std::string_view error_message) {
    if (check(expected)) {
        return advance();
    }
    set_error(error_message, current_token_);
    return Token(TokenType::INVALID, "", current_token_.location());
}

void Parser::set_error(std::string_view message, const Token& token) {
    if (status_.ok()) {
        std::string err = "Syntax error at line " + std::to_string(token.line()) +
                          ", col " + std::to_string(token.column()) + ": " +
                          std::string(message);
        if (!token.is_end_of_input() && !token.lexeme().empty()) {
            err += " (got '" + token.lexeme() + "')";
        } else if (token.is_end_of_input()) {
            err += " (got end of input)";
        }
        status_ = common::Status::SyntaxError(err);
    }
}

void Parser::synchronize() {
    advance();
    while (!check(TokenType::END_OF_INPUT)) {
        if (peek().is(TokenType::SEMICOLON)) {
            advance();
            return;
        }
        switch (peek().type()) {
            case TokenType::SELECT:
            case TokenType::CREATE:
            case TokenType::INSERT:
            case TokenType::UPDATE:
            case TokenType::DELETE:
                return;
            default:
                advance();
                break;
        }
    }
}

std::unique_ptr<Statement> Parser::parse_statement() {
    if (check(TokenType::END_OF_INPUT)) {
        return nullptr;
    }

    std::unique_ptr<Statement> stmt = nullptr;
    if (check(TokenType::SELECT)) {
        stmt = parse_select_statement();
    } else if (check(TokenType::CREATE)) {
        advance();
        if (check(TokenType::TABLE)) {
            stmt = parse_create_table_statement();
        } else if (check(TokenType::INDEX)) {
            stmt = parse_create_index_statement();
        } else {
            set_error("Expected 'TABLE' or 'INDEX' after 'CREATE'", peek());
            return nullptr;
        }
    } else if (check(TokenType::INSERT)) {
        stmt = parse_insert_statement();
    } else if (check(TokenType::UPDATE)) {
        stmt = parse_update_statement();
    } else if (check(TokenType::DELETE)) {
        stmt = parse_delete_statement();
    } else {
        set_error("Unexpected token; expected SQL statement (SELECT, CREATE, INSERT, UPDATE, DELETE)", peek());
        return nullptr;
    }

    if (stmt && status_.ok()) {
        match(TokenType::SEMICOLON); // consume optional trailing semicolon
    }

    return stmt;
}

std::vector<std::unique_ptr<Statement>> Parser::parse_statements() {
    std::vector<std::unique_ptr<Statement>> stmts;
    while (!check(TokenType::END_OF_INPUT)) {
        while (match(TokenType::SEMICOLON)) {}
        if (check(TokenType::END_OF_INPUT)) {
            break;
        }
        auto stmt = parse_statement();
        if (!stmt || has_error()) {
            break;
        }
        stmts.push_back(std::move(stmt));
    }
    return stmts;
}

std::unique_ptr<SelectStatement> Parser::parse_select_statement() {
    SourceLocation loc = peek().location();
    advance(); // consume SELECT

    auto stmt = std::make_unique<SelectStatement>(loc);

    if (match(TokenType::MULTIPLY)) {
        stmt->set_select_all(true);
    } else {
        auto first_col = parse_expression();
        if (!first_col || has_error()) {
            if (!has_error()) {
                set_error("Expected column list or '*' after 'SELECT'", peek());
            }
            return nullptr;
        }
        stmt->add_select_item(std::move(first_col));

        while (match(TokenType::COMMA)) {
            auto next_col = parse_expression();
            if (!next_col || has_error()) {
                if (!has_error()) {
                    set_error("Expected expression after comma in SELECT list", peek());
                }
                return nullptr;
            }
            stmt->add_select_item(std::move(next_col));
        }
    }

    consume(TokenType::FROM, "Expected 'FROM' in SELECT statement");
    if (has_error()) return nullptr;

    Token table_tok = consume(TokenType::IDENTIFIER, "Expected table name after 'FROM'");
    if (has_error()) return nullptr;
    stmt->set_from_table(table_tok.lexeme());

    if (match(TokenType::WHERE)) {
        auto where_expr = parse_expression();
        if (!where_expr || has_error()) {
            if (!has_error()) {
                set_error("Expected condition after 'WHERE'", peek());
            }
            return nullptr;
        }
        stmt->set_where_clause(std::move(where_expr));
    }

    if (match(TokenType::ORDER)) {
        consume(TokenType::BY, "Expected 'BY' after 'ORDER'");
        if (has_error()) return nullptr;

        auto order_expr = parse_expression();
        if (!order_expr || has_error()) {
            if (!has_error()) {
                set_error("Expected expression after 'ORDER BY'", peek());
            }
            return nullptr;
        }

        bool is_asc = true;
        if (match(TokenType::DESC)) {
            is_asc = false;
        } else if (match(TokenType::ASC)) {
            is_asc = true;
        }
        stmt->add_order_by(std::move(order_expr), is_asc);
    }

    if (match(TokenType::LIMIT)) {
        Token limit_tok = consume(TokenType::INTEGER_LITERAL, "Expected integer count after 'LIMIT'");
        if (has_error()) return nullptr;
        stmt->set_limit(std::stoll(limit_tok.lexeme()));
    }

    return stmt;
}

std::unique_ptr<CreateTableStatement> Parser::parse_create_table_statement() {
    SourceLocation loc = peek().location();
    advance(); // consume TABLE

    Token table_tok = consume(TokenType::IDENTIFIER, "Expected table name after 'CREATE TABLE'");
    if (has_error()) return nullptr;

    consume(TokenType::LEFT_PAREN, "Expected '(' after table name in CREATE TABLE");
    if (has_error()) return nullptr;

    auto stmt = std::make_unique<CreateTableStatement>(table_tok.lexeme(), loc);

    while (true) {
        if (match(TokenType::PRIMARY)) {
            // Table-level constraint: PRIMARY KEY (col1, col2)
            consume(TokenType::KEY, "Expected 'KEY' after 'PRIMARY'");
            if (has_error()) return nullptr;

            consume(TokenType::LEFT_PAREN, "Expected '(' after 'PRIMARY KEY'");
            if (has_error()) return nullptr;

            Token pk_col = consume(TokenType::IDENTIFIER, "Expected column name in PRIMARY KEY");
            if (has_error()) return nullptr;
            stmt->add_primary_key_column(pk_col.lexeme());

            while (match(TokenType::COMMA)) {
                Token next_pk = consume(TokenType::IDENTIFIER, "Expected column name after comma in PRIMARY KEY");
                if (has_error()) return nullptr;
                stmt->add_primary_key_column(next_pk.lexeme());
            }

            consume(TokenType::RIGHT_PAREN, "Expected ')' after PRIMARY KEY columns");
            if (has_error()) return nullptr;
        } else {
            // Column definition
            Token col_name = consume(TokenType::IDENTIFIER, "Expected column name in table definition");
            if (has_error()) return nullptr;

            DataType data_type;
            std::optional<std::size_t> length = std::nullopt;

            if (match(TokenType::INT)) {
                data_type = DataType::INT;
            } else if (match(TokenType::FLOAT)) {
                data_type = DataType::FLOAT;
            } else if (match(TokenType::TEXT)) {
                data_type = DataType::TEXT;
            } else if (match(TokenType::VARCHAR)) {
                data_type = DataType::VARCHAR;
                if (match(TokenType::LEFT_PAREN)) {
                    Token len_tok = consume(TokenType::INTEGER_LITERAL, "Expected integer length for VARCHAR");
                    if (has_error()) return nullptr;
                    length = static_cast<std::size_t>(std::stoull(len_tok.lexeme()));
                    consume(TokenType::RIGHT_PAREN, "Expected ')' after VARCHAR length");
                    if (has_error()) return nullptr;
                }
            } else {
                set_error("Expected data type (INT, FLOAT, TEXT, VARCHAR) for column '" + col_name.lexeme() + "'", peek());
                return nullptr;
            }

            bool is_primary_key = false;
            if (match(TokenType::PRIMARY)) {
                consume(TokenType::KEY, "Expected 'KEY' after 'PRIMARY'");
                if (has_error()) return nullptr;
                is_primary_key = true;
            }

            stmt->add_column(ColumnDefinition(col_name.lexeme(), data_type, is_primary_key, length));
        }

        if (!match(TokenType::COMMA)) {
            break;
        }
    }

    consume(TokenType::RIGHT_PAREN, "Expected ')' after column definitions");
    if (has_error()) return nullptr;

    return stmt;
}

std::unique_ptr<InsertStatement> Parser::parse_insert_statement() {
    SourceLocation loc = peek().location();
    advance(); // consume INSERT

    consume(TokenType::INTO, "Expected 'INTO' after 'INSERT'");
    if (has_error()) return nullptr;

    Token table_tok = consume(TokenType::IDENTIFIER, "Expected table name after 'INSERT INTO'");
    if (has_error()) return nullptr;

    auto stmt = std::make_unique<InsertStatement>(table_tok.lexeme(), loc);

    // Optional column list
    if (match(TokenType::LEFT_PAREN)) {
        Token col = consume(TokenType::IDENTIFIER, "Expected column name in INSERT column list");
        if (has_error()) return nullptr;
        stmt->add_column(col.lexeme());

        while (match(TokenType::COMMA)) {
            Token next_col = consume(TokenType::IDENTIFIER, "Expected column name after comma in column list");
            if (has_error()) return nullptr;
            stmt->add_column(next_col.lexeme());
        }

        consume(TokenType::RIGHT_PAREN, "Expected ')' after column list");
        if (has_error()) return nullptr;
    }

    consume(TokenType::VALUES, "Expected 'VALUES' in INSERT statement");
    if (has_error()) return nullptr;

    consume(TokenType::LEFT_PAREN, "Expected '(' before values list");
    if (has_error()) return nullptr;

    auto first_val = parse_expression();
    if (!first_val || has_error()) {
        if (!has_error()) {
            set_error("Expected value expression in VALUES list", peek());
        }
        return nullptr;
    }
    stmt->add_value(std::move(first_val));

    while (match(TokenType::COMMA)) {
        auto next_val = parse_expression();
        if (!next_val || has_error()) {
            if (!has_error()) {
                set_error("Expected value expression after comma in VALUES list", peek());
            }
            return nullptr;
        }
        stmt->add_value(std::move(next_val));
    }

    consume(TokenType::RIGHT_PAREN, "Expected ')' after values list");
    if (has_error()) return nullptr;

    return stmt;
}

std::unique_ptr<UpdateStatement> Parser::parse_update_statement() {
    SourceLocation loc = peek().location();
    advance(); // consume UPDATE

    Token table_tok = consume(TokenType::IDENTIFIER, "Expected table name after 'UPDATE'");
    if (has_error()) return nullptr;

    consume(TokenType::SET, "Expected 'SET' in UPDATE statement");
    if (has_error()) return nullptr;

    auto stmt = std::make_unique<UpdateStatement>(table_tok.lexeme(), loc);

    while (true) {
        Token col = consume(TokenType::IDENTIFIER, "Expected column name in UPDATE assignment");
        if (has_error()) return nullptr;

        consume(TokenType::EQUAL, "Expected '=' in UPDATE assignment");
        if (has_error()) return nullptr;

        auto val = parse_expression();
        if (!val || has_error()) {
            if (!has_error()) {
                set_error("Expected expression on right side of '=' in UPDATE assignment", peek());
            }
            return nullptr;
        }

        stmt->add_assignment(col.lexeme(), std::move(val));

        if (!match(TokenType::COMMA)) {
            break;
        }
    }

    if (match(TokenType::WHERE)) {
        auto where_expr = parse_expression();
        if (!where_expr || has_error()) {
            if (!has_error()) {
                set_error("Expected condition after 'WHERE'", peek());
            }
            return nullptr;
        }
        stmt->set_where_clause(std::move(where_expr));
    }

    return stmt;
}

std::unique_ptr<DeleteStatement> Parser::parse_delete_statement() {
    SourceLocation loc = peek().location();
    advance(); // consume DELETE

    consume(TokenType::FROM, "Expected 'FROM' after 'DELETE'");
    if (has_error()) return nullptr;

    Token table_tok = consume(TokenType::IDENTIFIER, "Expected table name after 'DELETE FROM'");
    if (has_error()) return nullptr;

    auto stmt = std::make_unique<DeleteStatement>(table_tok.lexeme(), loc);

    if (match(TokenType::WHERE)) {
        auto where_expr = parse_expression();
        if (!where_expr || has_error()) {
            if (!has_error()) {
                set_error("Expected condition after 'WHERE'", peek());
            }
            return nullptr;
        }
        stmt->set_where_clause(std::move(where_expr));
    }

    return stmt;
}

std::unique_ptr<CreateIndexStatement> Parser::parse_create_index_statement() {
    SourceLocation loc = peek().location();
    advance(); // consume INDEX

    Token idx_tok = consume(TokenType::IDENTIFIER, "Expected index name after 'CREATE INDEX'");
    if (has_error()) return nullptr;

    consume(TokenType::ON, "Expected 'ON' after index name");
    if (has_error()) return nullptr;

    Token table_tok = consume(TokenType::IDENTIFIER, "Expected table name after 'ON'");
    if (has_error()) return nullptr;

    consume(TokenType::LEFT_PAREN, "Expected '(' before index column list");
    if (has_error()) return nullptr;

    auto stmt = std::make_unique<CreateIndexStatement>(idx_tok.lexeme(), table_tok.lexeme(), loc);

    Token col = consume(TokenType::IDENTIFIER, "Expected column name in index column list");
    if (has_error()) return nullptr;
    stmt->add_column(col.lexeme());

    while (match(TokenType::COMMA)) {
        Token next_col = consume(TokenType::IDENTIFIER, "Expected column name after comma in index column list");
        if (has_error()) return nullptr;
        stmt->add_column(next_col.lexeme());
    }

    consume(TokenType::RIGHT_PAREN, "Expected ')' after index column list");
    if (has_error()) return nullptr;

    return stmt;
}

// -----------------------------------------------------------------------------
// Expression Parsing with Operator Precedence
// -----------------------------------------------------------------------------

std::unique_ptr<Expression> Parser::parse_expression() {
    return parse_or_expression();
}

std::unique_ptr<Expression> Parser::parse_or_expression() {
    auto expr = parse_and_expression();
    if (!expr || has_error()) return nullptr;

    while (match(TokenType::OR)) {
        SourceLocation loc = peek().location();
        auto right = parse_and_expression();
        if (!right || has_error()) {
            if (!has_error()) set_error("Expected expression after 'OR'", peek());
            return nullptr;
        }
        expr = std::make_unique<BinaryExpression>(BinaryOp::Or, std::move(expr), std::move(right), loc);
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parse_and_expression() {
    auto expr = parse_not_expression();
    if (!expr || has_error()) return nullptr;

    while (match(TokenType::AND)) {
        SourceLocation loc = peek().location();
        auto right = parse_not_expression();
        if (!right || has_error()) {
            if (!has_error()) set_error("Expected expression after 'AND'", peek());
            return nullptr;
        }
        expr = std::make_unique<BinaryExpression>(BinaryOp::And, std::move(expr), std::move(right), loc);
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parse_not_expression() {
    if (match(TokenType::NOT)) {
        SourceLocation loc = peek().location();
        auto operand = parse_not_expression();
        if (!operand || has_error()) {
            if (!has_error()) set_error("Expected expression after 'NOT'", peek());
            return nullptr;
        }
        return std::make_unique<UnaryExpression>(UnaryOp::Not, std::move(operand), loc);
    }
    return parse_comparison_expression();
}

std::unique_ptr<Expression> Parser::parse_comparison_expression() {
    auto expr = parse_additive_expression();
    if (!expr || has_error()) return nullptr;

    std::optional<BinaryOp> op = std::nullopt;
    SourceLocation loc = peek().location();

    if (match(TokenType::EQUAL)) {
        op = BinaryOp::Equal;
    } else if (match(TokenType::NOT_EQUAL)) {
        op = BinaryOp::NotEqual;
    } else if (match(TokenType::LESS_THAN)) {
        op = BinaryOp::LessThan;
    } else if (match(TokenType::LESS_EQUAL)) {
        op = BinaryOp::LessEqual;
    } else if (match(TokenType::GREATER_THAN)) {
        op = BinaryOp::GreaterThan;
    } else if (match(TokenType::GREATER_EQUAL)) {
        op = BinaryOp::GreaterEqual;
    }

    if (op.has_value()) {
        auto right = parse_additive_expression();
        if (!right || has_error()) {
            if (!has_error()) set_error("Expected expression on right side of comparison", peek());
            return nullptr;
        }
        expr = std::make_unique<BinaryExpression>(*op, std::move(expr), std::move(right), loc);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parse_additive_expression() {
    auto expr = parse_multiplicative_expression();
    if (!expr || has_error()) return nullptr;

    while (true) {
        BinaryOp op;
        SourceLocation loc = peek().location();

        if (match(TokenType::PLUS)) {
            op = BinaryOp::Add;
        } else if (match(TokenType::MINUS)) {
            op = BinaryOp::Subtract;
        } else {
            break;
        }

        auto right = parse_multiplicative_expression();
        if (!right || has_error()) {
            if (!has_error()) set_error("Expected expression on right side of '+' or '-'", peek());
            return nullptr;
        }
        expr = std::make_unique<BinaryExpression>(op, std::move(expr), std::move(right), loc);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parse_multiplicative_expression() {
    auto expr = parse_unary_expression();
    if (!expr || has_error()) return nullptr;

    while (true) {
        BinaryOp op;
        SourceLocation loc = peek().location();

        if (match(TokenType::MULTIPLY)) {
            op = BinaryOp::Multiply;
        } else if (match(TokenType::DIVIDE)) {
            op = BinaryOp::Divide;
        } else {
            break;
        }

        auto right = parse_unary_expression();
        if (!right || has_error()) {
            if (!has_error()) set_error("Expected expression on right side of '*' or '/'", peek());
            return nullptr;
        }
        expr = std::make_unique<BinaryExpression>(op, std::move(expr), std::move(right), loc);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parse_unary_expression() {
    if (match(TokenType::PLUS)) {
        SourceLocation loc = peek().location();
        auto operand = parse_unary_expression();
        if (!operand || has_error()) return nullptr;
        return std::make_unique<UnaryExpression>(UnaryOp::Plus, std::move(operand), loc);
    }
    if (match(TokenType::MINUS)) {
        SourceLocation loc = peek().location();
        auto operand = parse_unary_expression();
        if (!operand || has_error()) return nullptr;
        return std::make_unique<UnaryExpression>(UnaryOp::Minus, std::move(operand), loc);
    }
    return parse_primary_expression();
}

std::unique_ptr<Expression> Parser::parse_primary_expression() {
    SourceLocation loc = peek().location();

    if (match(TokenType::LEFT_PAREN)) {
        auto expr = parse_expression();
        if (!expr || has_error()) return nullptr;
        consume(TokenType::RIGHT_PAREN, "Expected ')' after parenthesized expression");
        if (has_error()) return nullptr;
        return expr;
    }

    if (check(TokenType::INTEGER_LITERAL)) {
        Token tok = advance();
        return LiteralExpression::make_int(std::stoll(tok.lexeme()), loc);
    }

    if (check(TokenType::FLOAT_LITERAL)) {
        Token tok = advance();
        return LiteralExpression::make_float(std::stod(tok.lexeme()), loc);
    }

    if (check(TokenType::STRING_LITERAL)) {
        Token tok = advance();
        std::string unquoted = unquote_sql_string(tok.lexeme());
        return LiteralExpression::make_string(std::move(unquoted), loc);
    }

    if (check(TokenType::NULL_KEYWORD)) {
        advance();
        return LiteralExpression::make_null(loc);
    }

    if (check(TokenType::IDENTIFIER)) {
        Token id_tok = advance();
        if (match(TokenType::DOT)) {
            Token col_tok = consume(TokenType::IDENTIFIER, "Expected column name after '.'");
            if (has_error()) return nullptr;
            return std::make_unique<ColumnRefExpression>(col_tok.lexeme(), id_tok.lexeme(), loc);
        }
        return std::make_unique<ColumnRefExpression>(id_tok.lexeme(), "", loc);
    }

    set_error("Unexpected token in expression", peek());
    return nullptr;
}

std::pair<std::unique_ptr<Statement>, common::Status> Parser::parse(std::string_view sql) {
    Parser parser(sql);
    auto stmt = parser.parse_statement();
    if (stmt && parser.status().ok()) {
        while (parser.match(TokenType::SEMICOLON)) {}
        if (!parser.check(TokenType::END_OF_INPUT)) {
            parser.set_error("Unexpected trailing token after SQL statement", parser.peek());
            return {nullptr, parser.status()};
        }
    }
    return {std::move(stmt), parser.status()};
}

} // namespace querycraft::parser
