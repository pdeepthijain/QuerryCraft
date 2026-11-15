#include <gtest/gtest.h>
#include "parser/lexer.hpp"

using namespace querycraft::parser;
using querycraft::common::StatusCode;

TEST(LexerTest, SimpleSelectStatement) {
    std::string sql = "SELECT name, cgpa FROM students WHERE cgpa > 9.0;";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 12);
    EXPECT_EQ(tokens[0].type(), TokenType::SELECT);
    EXPECT_EQ(tokens[0].format(), "KEYWORD(SELECT)");

    EXPECT_EQ(tokens[1].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].lexeme(), "name");
    EXPECT_EQ(tokens[1].format(), "IDENTIFIER(name)");

    EXPECT_EQ(tokens[2].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[2].format(), "COMMA");

    EXPECT_EQ(tokens[3].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[3].lexeme(), "cgpa");
    EXPECT_EQ(tokens[3].format(), "IDENTIFIER(cgpa)");

    EXPECT_EQ(tokens[4].type(), TokenType::FROM);
    EXPECT_EQ(tokens[4].format(), "KEYWORD(FROM)");

    EXPECT_EQ(tokens[5].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[5].lexeme(), "students");
    EXPECT_EQ(tokens[5].format(), "IDENTIFIER(students)");

    EXPECT_EQ(tokens[6].type(), TokenType::WHERE);
    EXPECT_EQ(tokens[6].format(), "KEYWORD(WHERE)");

    EXPECT_EQ(tokens[7].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[7].lexeme(), "cgpa");
    EXPECT_EQ(tokens[7].format(), "IDENTIFIER(cgpa)");

    EXPECT_EQ(tokens[8].type(), TokenType::GREATER_THAN);
    EXPECT_EQ(tokens[8].format(), "GREATER_THAN");

    EXPECT_EQ(tokens[9].type(), TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[9].lexeme(), "9.0");
    EXPECT_EQ(tokens[9].format(), "FLOAT_LITERAL(9.0)");

    EXPECT_EQ(tokens[10].type(), TokenType::SEMICOLON);
    EXPECT_EQ(tokens[10].format(), "SEMICOLON");

    EXPECT_EQ(tokens[11].type(), TokenType::END_OF_INPUT);
    EXPECT_EQ(tokens[11].format(), "END_OF_INPUT");
}

TEST(LexerTest, CreateTableStatement) {
    std::string sql = "CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR, cgpa FLOAT, notes TEXT);";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 20);
    EXPECT_EQ(tokens[0].type(), TokenType::CREATE);
    EXPECT_EQ(tokens[1].type(), TokenType::TABLE);
    EXPECT_EQ(tokens[2].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].lexeme(), "students");
    EXPECT_EQ(tokens[3].type(), TokenType::LEFT_PAREN);
    EXPECT_EQ(tokens[4].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[4].lexeme(), "id");
    EXPECT_EQ(tokens[5].type(), TokenType::INT);
    EXPECT_EQ(tokens[6].type(), TokenType::PRIMARY);
    EXPECT_EQ(tokens[7].type(), TokenType::KEY);
    EXPECT_EQ(tokens[8].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[9].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[9].lexeme(), "name");
    EXPECT_EQ(tokens[10].type(), TokenType::VARCHAR);
    EXPECT_EQ(tokens[11].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[12].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[12].lexeme(), "cgpa");
    EXPECT_EQ(tokens[13].type(), TokenType::FLOAT);
    EXPECT_EQ(tokens[14].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[15].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[15].lexeme(), "notes");
    EXPECT_EQ(tokens[16].type(), TokenType::TEXT);
    EXPECT_EQ(tokens[17].type(), TokenType::RIGHT_PAREN);
    EXPECT_EQ(tokens[18].type(), TokenType::SEMICOLON);
    EXPECT_EQ(tokens[19].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, InsertStatement) {
    std::string sql = "INSERT INTO students (id, name, cgpa) VALUES (1, 'Alice', 9.5);";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 20);
    EXPECT_EQ(tokens[0].type(), TokenType::INSERT);
    EXPECT_EQ(tokens[1].type(), TokenType::INTO);
    EXPECT_EQ(tokens[2].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].lexeme(), "students");
    EXPECT_EQ(tokens[3].type(), TokenType::LEFT_PAREN);
    EXPECT_EQ(tokens[4].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[4].lexeme(), "id");
    EXPECT_EQ(tokens[5].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[6].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[6].lexeme(), "name");
    EXPECT_EQ(tokens[7].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[8].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[8].lexeme(), "cgpa");
    EXPECT_EQ(tokens[9].type(), TokenType::RIGHT_PAREN);
    EXPECT_EQ(tokens[10].type(), TokenType::VALUES);
    EXPECT_EQ(tokens[11].type(), TokenType::LEFT_PAREN);
    EXPECT_EQ(tokens[12].type(), TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tokens[12].lexeme(), "1");
    EXPECT_EQ(tokens[13].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[14].type(), TokenType::STRING_LITERAL);
    EXPECT_EQ(tokens[14].lexeme(), "'Alice'");
    EXPECT_EQ(tokens[15].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[16].type(), TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[16].lexeme(), "9.5");
    EXPECT_EQ(tokens[17].type(), TokenType::RIGHT_PAREN);
    EXPECT_EQ(tokens[18].type(), TokenType::SEMICOLON);
    EXPECT_EQ(tokens[19].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, CaseInsensitiveKeywords) {
    std::string sql = "select FrOm wHeRe OrDeR bY dEsC LiMiT JoIn GrOuP HaViNg NuLl";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 12);
    EXPECT_EQ(tokens[0].type(), TokenType::SELECT);
    EXPECT_EQ(tokens[0].lexeme(), "select"); // Preserves original text
    EXPECT_EQ(tokens[1].type(), TokenType::FROM);
    EXPECT_EQ(tokens[2].type(), TokenType::WHERE);
    EXPECT_EQ(tokens[3].type(), TokenType::ORDER);
    EXPECT_EQ(tokens[4].type(), TokenType::BY);
    EXPECT_EQ(tokens[5].type(), TokenType::DESC);
    EXPECT_EQ(tokens[6].type(), TokenType::LIMIT);
    EXPECT_EQ(tokens[7].type(), TokenType::JOIN);
    EXPECT_EQ(tokens[8].type(), TokenType::GROUP);
    EXPECT_EQ(tokens[9].type(), TokenType::HAVING);
    EXPECT_EQ(tokens[10].type(), TokenType::NULL_KEYWORD);
    EXPECT_EQ(tokens[11].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, AllKeywordsCoverage) {
    std::string sql =
        "SELECT FROM WHERE CREATE TABLE INSERT INTO VALUES "
        "UPDATE SET DELETE DROP INDEX ON AND OR NOT NULL "
        "INT FLOAT TEXT VARCHAR PRIMARY KEY ORDER BY ASC DESC "
        "LIMIT JOIN INNER LEFT GROUP HAVING";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 35); // 34 keywords + END_OF_INPUT
    for (std::size_t i = 0; i < 34; ++i) {
        EXPECT_TRUE(tokens[i].is_keyword());
    }
    EXPECT_EQ(tokens[34].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, IdentifiersWithUnderscoresAndDigits) {
    std::string sql = "_id user_name table_2026 __sys_col__";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[0].lexeme(), "_id");
    EXPECT_EQ(tokens[1].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].lexeme(), "user_name");
    EXPECT_EQ(tokens[2].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].lexeme(), "table_2026");
    EXPECT_EQ(tokens[3].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[3].lexeme(), "__sys_col__");
    EXPECT_EQ(tokens[4].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, IdentifiersWithKeywordPrefixes) {
    std::string sql = "select_all from_date wherever orders primary_key nullable group_by";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 8);
    for (std::size_t i = 0; i < 7; ++i) {
        EXPECT_EQ(tokens[i].type(), TokenType::IDENTIFIER);
    }
}

TEST(LexerTest, NumericLiterals) {
    std::string sql = "0 42 1000 0.0 9.0 3.14159 .5 1e5 2.5e-3";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 10);
    EXPECT_EQ(tokens[0].type(), TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tokens[0].lexeme(), "0");

    EXPECT_EQ(tokens[1].type(), TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tokens[1].lexeme(), "42");

    EXPECT_EQ(tokens[2].type(), TokenType::INTEGER_LITERAL);
    EXPECT_EQ(tokens[2].lexeme(), "1000");

    EXPECT_EQ(tokens[3].type(), TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[3].lexeme(), "0.0");

    EXPECT_EQ(tokens[4].type(), TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[4].lexeme(), "9.0");

    EXPECT_EQ(tokens[5].type(), TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[5].lexeme(), "3.14159");

    EXPECT_EQ(tokens[6].type(), TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[6].lexeme(), ".5");

    EXPECT_EQ(tokens[7].type(), TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[7].lexeme(), "1e5");

    EXPECT_EQ(tokens[8].type(), TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[8].lexeme(), "2.5e-3");
}

TEST(LexerTest, StringLiteralsAndEscapedQuotes) {
    std::string sql = "'hello' 'O''Reilly' '' 'with spaces and 123;'";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type(), TokenType::STRING_LITERAL);
    EXPECT_EQ(tokens[0].lexeme(), "'hello'");

    EXPECT_EQ(tokens[1].type(), TokenType::STRING_LITERAL);
    EXPECT_EQ(tokens[1].lexeme(), "'O''Reilly'");

    EXPECT_EQ(tokens[2].type(), TokenType::STRING_LITERAL);
    EXPECT_EQ(tokens[2].lexeme(), "''");

    EXPECT_EQ(tokens[3].type(), TokenType::STRING_LITERAL);
    EXPECT_EQ(tokens[3].lexeme(), "'with spaces and 123;'");
}

TEST(LexerTest, ComparisonOperators) {
    std::string sql = "= != <> < <= > >=";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 8);
    EXPECT_EQ(tokens[0].type(), TokenType::EQUAL);
    EXPECT_EQ(tokens[1].type(), TokenType::NOT_EQUAL);
    EXPECT_EQ(tokens[1].lexeme(), "!=");
    EXPECT_EQ(tokens[2].type(), TokenType::NOT_EQUAL);
    EXPECT_EQ(tokens[2].lexeme(), "<>");
    EXPECT_EQ(tokens[3].type(), TokenType::LESS_THAN);
    EXPECT_EQ(tokens[4].type(), TokenType::LESS_EQUAL);
    EXPECT_EQ(tokens[5].type(), TokenType::GREATER_THAN);
    EXPECT_EQ(tokens[6].type(), TokenType::GREATER_EQUAL);
    EXPECT_EQ(tokens[7].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, ArithmeticOperators) {
    std::string sql = "+ - * /";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type(), TokenType::PLUS);
    EXPECT_EQ(tokens[1].type(), TokenType::MINUS);
    EXPECT_EQ(tokens[2].type(), TokenType::MULTIPLY);
    EXPECT_EQ(tokens[3].type(), TokenType::DIVIDE);
    EXPECT_EQ(tokens[4].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, Punctuation) {
    std::string sql = "( ) , . ;";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type(), TokenType::LEFT_PAREN);
    EXPECT_EQ(tokens[1].type(), TokenType::RIGHT_PAREN);
    EXPECT_EQ(tokens[2].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[3].type(), TokenType::DOT);
    EXPECT_EQ(tokens[4].type(), TokenType::SEMICOLON);
    EXPECT_EQ(tokens[5].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, WhitespaceAndCommentsSkipped) {
    std::string sql =
        "-- Leading comment line\n"
        "SELECT -- Inline comment\n"
        "    id, name\n"
        "-- Intermediate comment\n"
        "FROM students; -- Trailing comment";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.has_error());

    ASSERT_EQ(tokens.size(), 8);
    EXPECT_EQ(tokens[0].type(), TokenType::SELECT);
    EXPECT_EQ(tokens[1].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].lexeme(), "id");
    EXPECT_EQ(tokens[2].type(), TokenType::COMMA);
    EXPECT_EQ(tokens[3].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[3].lexeme(), "name");
    EXPECT_EQ(tokens[4].type(), TokenType::FROM);
    EXPECT_EQ(tokens[5].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[5].lexeme(), "students");
    EXPECT_EQ(tokens[6].type(), TokenType::SEMICOLON);
    EXPECT_EQ(tokens[7].type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, UnterminatedStringError) {
    std::string sql = "SELECT * FROM users WHERE name = 'unterminated";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_TRUE(lexer.has_error());
    EXPECT_EQ(lexer.status().code(), StatusCode::SyntaxError);
    EXPECT_NE(lexer.status().message().find("Unterminated string"), std::string::npos);

    ASSERT_GE(tokens.size(), 8);
    EXPECT_EQ(tokens[7].type(), TokenType::INVALID);
    EXPECT_EQ(tokens[7].lexeme(), "'unterminated");
}

TEST(LexerTest, InvalidCharactersError) {
    std::string sql = "SELECT @user, #count, $price FROM items;";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_TRUE(lexer.has_error());
    EXPECT_EQ(lexer.status().code(), StatusCode::SyntaxError);

    // The first invalid character '@' was captured
    EXPECT_NE(lexer.status().message().find("Unexpected character"), std::string::npos);

    // Verify invalid tokens are present with correct lexemes
    bool found_at = false;
    for (const auto& t : tokens) {
        if (t.type() == TokenType::INVALID && t.lexeme() == "@") {
            found_at = true;
        }
    }
    EXPECT_TRUE(found_at);
}

TEST(LexerTest, MalformedNumericLiteralWithMultipleDots) {
    std::string sql = "SELECT 9.0.0 FROM t;";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_TRUE(lexer.has_error());
    EXPECT_EQ(lexer.status().code(), StatusCode::SyntaxError);
    EXPECT_NE(lexer.status().message().find("multiple decimal points"), std::string::npos);

    ASSERT_GE(tokens.size(), 2);
    EXPECT_EQ(tokens[1].type(), TokenType::INVALID);
    EXPECT_EQ(tokens[1].lexeme(), "9.0.0");
}

TEST(LexerTest, MalformedNumericLiteralWithLetters) {
    std::string sql = "SELECT 123abc_xyz FROM t;";
    Lexer lexer(sql);

    auto tokens = lexer.tokenize();
    EXPECT_TRUE(lexer.has_error());
    EXPECT_EQ(lexer.status().code(), StatusCode::SyntaxError);
    EXPECT_NE(lexer.status().message().find("followed by letters"), std::string::npos);

    ASSERT_GE(tokens.size(), 2);
    EXPECT_EQ(tokens[1].type(), TokenType::INVALID);
    EXPECT_EQ(tokens[1].lexeme(), "123abc_xyz");
}

TEST(LexerTest, EmptyAndWhitespaceInput) {
    {
        Lexer lexer("");
        auto tokens = lexer.tokenize();
        ASSERT_EQ(tokens.size(), 1);
        EXPECT_EQ(tokens[0].type(), TokenType::END_OF_INPUT);
        EXPECT_FALSE(lexer.has_error());
    }
    {
        Lexer lexer("   \t\r\n\f  ");
        auto tokens = lexer.tokenize();
        ASSERT_EQ(tokens.size(), 1);
        EXPECT_EQ(tokens[0].type(), TokenType::END_OF_INPUT);
        EXPECT_FALSE(lexer.has_error());
    }
    {
        Lexer lexer("-- Just a comment\n");
        auto tokens = lexer.tokenize();
        ASSERT_EQ(tokens.size(), 1);
        EXPECT_EQ(tokens[0].type(), TokenType::END_OF_INPUT);
        EXPECT_FALSE(lexer.has_error());
    }
}

TEST(LexerTest, EndOfInputRepeatedCalls) {
    Lexer lexer("SELECT 1;");
    EXPECT_EQ(lexer.next_token().type(), TokenType::SELECT);
    EXPECT_EQ(lexer.next_token().type(), TokenType::INTEGER_LITERAL);
    EXPECT_EQ(lexer.next_token().type(), TokenType::SEMICOLON);
    EXPECT_EQ(lexer.next_token().type(), TokenType::END_OF_INPUT);
    // Subsequent calls safely return END_OF_INPUT
    EXPECT_EQ(lexer.next_token().type(), TokenType::END_OF_INPUT);
    EXPECT_EQ(lexer.next_token().type(), TokenType::END_OF_INPUT);
}

TEST(LexerTest, LineAndColumnCoordinates) {
    std::string sql =
        "SELECT\n"
        "    id,\n"
        "    name\n"
        "FROM students;";
    Lexer lexer(sql);

    Token t1 = lexer.next_token();
    EXPECT_EQ(t1.type(), TokenType::SELECT);
    EXPECT_EQ(t1.line(), 1);
    EXPECT_EQ(t1.column(), 1);

    Token t2 = lexer.next_token();
    EXPECT_EQ(t2.type(), TokenType::IDENTIFIER);
    EXPECT_EQ(t2.lexeme(), "id");
    EXPECT_EQ(t2.line(), 2);
    EXPECT_EQ(t2.column(), 5);

    Token t3 = lexer.next_token();
    EXPECT_EQ(t3.type(), TokenType::COMMA);
    EXPECT_EQ(t3.line(), 2);
    EXPECT_EQ(t3.column(), 7);

    Token t4 = lexer.next_token();
    EXPECT_EQ(t4.type(), TokenType::IDENTIFIER);
    EXPECT_EQ(t4.lexeme(), "name");
    EXPECT_EQ(t4.line(), 3);
    EXPECT_EQ(t4.column(), 5);

    Token t5 = lexer.next_token();
    EXPECT_EQ(t5.type(), TokenType::FROM);
    EXPECT_EQ(t5.line(), 4);
    EXPECT_EQ(t5.column(), 1);
}

TEST(LexerTest, PeekTokenDoesNotAdvanceCursor) {
    std::string sql = "SELECT * FROM users;";
    Lexer lexer(sql);

    Token peeked1 = lexer.peek_token();
    EXPECT_EQ(peeked1.type(), TokenType::SELECT);

    Token peeked2 = lexer.peek_token();
    EXPECT_EQ(peeked2.type(), TokenType::SELECT);

    Token consumed = lexer.next_token();
    EXPECT_EQ(consumed.type(), TokenType::SELECT);

    Token next_peek = lexer.peek_token();
    EXPECT_EQ(next_peek.type(), TokenType::MULTIPLY);

    Token next_consumed = lexer.next_token();
    EXPECT_EQ(next_consumed.type(), TokenType::MULTIPLY);
}

TEST(LexerTest, ResetFunctionality) {
    Lexer lexer("SELECT 1;");
    auto tokens1 = lexer.tokenize();
    EXPECT_EQ(tokens1[0].type(), TokenType::SELECT);

    lexer.reset("DELETE FROM t;");
    auto tokens2 = lexer.tokenize();
    EXPECT_EQ(tokens2[0].type(), TokenType::DELETE);
    EXPECT_EQ(tokens2[1].type(), TokenType::FROM);
    EXPECT_EQ(tokens2[2].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens2[3].type(), TokenType::SEMICOLON);
}
