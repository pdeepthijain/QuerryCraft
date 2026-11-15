#include <gtest/gtest.h>
#include "parser/parser.hpp"

using namespace querycraft::parser;
using querycraft::common::StatusCode;

TEST(ParserTest, SimpleSelect) {
    std::string sql = "SELECT name FROM students;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(stmt->stmt_type(), StatementType::SELECT);

    auto* select = dynamic_cast<SelectStatement*>(stmt.get());
    ASSERT_NE(select, nullptr);
    EXPECT_FALSE(select->select_all());
    EXPECT_EQ(select->from_table(), "students");
    ASSERT_EQ(select->select_list().size(), 1);
    EXPECT_EQ(select->select_list()[0]->to_string(), "name");
    EXPECT_EQ(select->where_clause(), nullptr);
}

TEST(ParserTest, SelectAllAsterisk) {
    std::string sql = "SELECT * FROM students;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* select = dynamic_cast<SelectStatement*>(stmt.get());
    ASSERT_NE(select, nullptr);
    EXPECT_TRUE(select->select_all());
    EXPECT_EQ(select->from_table(), "students");
    EXPECT_TRUE(select->select_list().empty());
}

TEST(ParserTest, SelectMultipleColumns) {
    std::string sql = "SELECT id, name, cgpa, department FROM students;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* select = dynamic_cast<SelectStatement*>(stmt.get());
    ASSERT_NE(select, nullptr);
    EXPECT_FALSE(select->select_all());
    EXPECT_EQ(select->from_table(), "students");
    ASSERT_EQ(select->select_list().size(), 4);
    EXPECT_EQ(select->select_list()[0]->to_string(), "id");
    EXPECT_EQ(select->select_list()[1]->to_string(), "name");
    EXPECT_EQ(select->select_list()[2]->to_string(), "cgpa");
    EXPECT_EQ(select->select_list()[3]->to_string(), "department");
}

TEST(ParserTest, SelectWithWhere) {
    std::string sql = "SELECT name, cgpa FROM students WHERE cgpa > 9.0;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* select = dynamic_cast<SelectStatement*>(stmt.get());
    ASSERT_NE(select, nullptr);
    EXPECT_EQ(select->from_table(), "students");
    ASSERT_EQ(select->select_list().size(), 2);

    ASSERT_NE(select->where_clause(), nullptr);
    EXPECT_EQ(select->where_clause()->expr_type(), ExpressionType::BINARY);

    const auto* bin = dynamic_cast<const BinaryExpression*>(select->where_clause());
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op(), BinaryOp::GreaterThan);
    EXPECT_EQ(bin->left()->to_string(), "cgpa");
    EXPECT_EQ(bin->right()->to_string(), "9.0");
}

TEST(ParserTest, SelectWithOrderByAndLimit) {
    std::string sql = "SELECT name, cgpa FROM students ORDER BY cgpa DESC LIMIT 10;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* select = dynamic_cast<SelectStatement*>(stmt.get());
    ASSERT_NE(select, nullptr);

    ASSERT_EQ(select->order_by().size(), 1);
    EXPECT_FALSE(select->order_by()[0].is_asc);
    EXPECT_EQ(select->order_by()[0].expr->to_string(), "cgpa");

    ASSERT_TRUE(select->limit().has_value());
    EXPECT_EQ(*select->limit(), 10);
}

TEST(ParserTest, SelectWithOrderByAsc) {
    std::string sql = "SELECT name FROM students ORDER BY name ASC;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* select = dynamic_cast<SelectStatement*>(stmt.get());
    ASSERT_NE(select, nullptr);
    ASSERT_EQ(select->order_by().size(), 1);
    EXPECT_TRUE(select->order_by()[0].is_asc);
    EXPECT_EQ(select->order_by()[0].expr->to_string(), "name");
}

TEST(ParserTest, CreateTableSimple) {
    std::string sql = "CREATE TABLE users (id INT, name VARCHAR, score FLOAT, bio TEXT);";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* create = dynamic_cast<CreateTableStatement*>(stmt.get());
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->table_name(), "users");
    ASSERT_EQ(create->columns().size(), 4);

    EXPECT_EQ(create->columns()[0].name, "id");
    EXPECT_EQ(create->columns()[0].data_type, DataType::INT);
    EXPECT_FALSE(create->columns()[0].is_primary_key);

    EXPECT_EQ(create->columns()[1].name, "name");
    EXPECT_EQ(create->columns()[1].data_type, DataType::VARCHAR);

    EXPECT_EQ(create->columns()[2].name, "score");
    EXPECT_EQ(create->columns()[2].data_type, DataType::FLOAT);

    EXPECT_EQ(create->columns()[3].name, "bio");
    EXPECT_EQ(create->columns()[3].data_type, DataType::TEXT);
}

TEST(ParserTest, CreateTableWithPrimaryKeyColumn) {
    std::string sql = "CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(255));";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* create = dynamic_cast<CreateTableStatement*>(stmt.get());
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->table_name(), "students");
    ASSERT_EQ(create->columns().size(), 2);

    EXPECT_EQ(create->columns()[0].name, "id");
    EXPECT_TRUE(create->columns()[0].is_primary_key);

    EXPECT_EQ(create->columns()[1].name, "name");
    ASSERT_TRUE(create->columns()[1].length.has_value());
    EXPECT_EQ(*create->columns()[1].length, 255);

    ASSERT_EQ(create->primary_key_columns().size(), 1);
    EXPECT_EQ(create->primary_key_columns()[0], "id");
}

TEST(ParserTest, CreateTableWithTableLevelPrimaryKey) {
    std::string sql = "CREATE TABLE enrollments (student_id INT, course_id INT, PRIMARY KEY (student_id, course_id));";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* create = dynamic_cast<CreateTableStatement*>(stmt.get());
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->columns().size(), 2);
    ASSERT_EQ(create->primary_key_columns().size(), 2);
    EXPECT_EQ(create->primary_key_columns()[0], "student_id");
    EXPECT_EQ(create->primary_key_columns()[1], "course_id");
}

TEST(ParserTest, InsertWithColumns) {
    std::string sql = "INSERT INTO students (id, name, cgpa) VALUES (1, 'Alice', 9.5);";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* insert = dynamic_cast<InsertStatement*>(stmt.get());
    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(insert->table_name(), "students");
    ASSERT_EQ(insert->columns().size(), 3);
    EXPECT_EQ(insert->columns()[0], "id");
    EXPECT_EQ(insert->columns()[1], "name");
    EXPECT_EQ(insert->columns()[2], "cgpa");

    ASSERT_EQ(insert->values().size(), 3);
    EXPECT_EQ(insert->values()[0]->to_string(), "1");
    EXPECT_EQ(insert->values()[1]->to_string(), "'Alice'");
    EXPECT_EQ(insert->values()[2]->to_string(), "9.5");
}

TEST(ParserTest, InsertWithoutColumns) {
    std::string sql = "INSERT INTO students VALUES (2, 'Bob', 8.7);";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* insert = dynamic_cast<InsertStatement*>(stmt.get());
    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(insert->table_name(), "students");
    EXPECT_TRUE(insert->columns().empty());
    ASSERT_EQ(insert->values().size(), 3);
    EXPECT_EQ(insert->values()[0]->to_string(), "2");
    EXPECT_EQ(insert->values()[1]->to_string(), "'Bob'");
    EXPECT_EQ(insert->values()[2]->to_string(), "8.7");
}

TEST(ParserTest, UpdateStatement) {
    std::string sql = "UPDATE students SET cgpa = 9.8, name = 'Alice Cooper' WHERE id = 1;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* update = dynamic_cast<UpdateStatement*>(stmt.get());
    ASSERT_NE(update, nullptr);
    EXPECT_EQ(update->table_name(), "students");
    ASSERT_EQ(update->assignments().size(), 2);
    EXPECT_EQ(update->assignments()[0].column, "cgpa");
    EXPECT_EQ(update->assignments()[0].value->to_string(), "9.8");
    EXPECT_EQ(update->assignments()[1].column, "name");
    EXPECT_EQ(update->assignments()[1].value->to_string(), "'Alice Cooper'");

    ASSERT_NE(update->where_clause(), nullptr);
    EXPECT_EQ(update->where_clause()->to_string(), "(id = 1)");
}

TEST(ParserTest, DeleteStatement) {
    std::string sql = "DELETE FROM students WHERE cgpa < 5.0;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* del = dynamic_cast<DeleteStatement*>(stmt.get());
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(del->table_name(), "students");
    ASSERT_NE(del->where_clause(), nullptr);
    EXPECT_EQ(del->where_clause()->to_string(), "(cgpa < 5.0)");
}

TEST(ParserTest, DeleteWithoutWhere) {
    std::string sql = "DELETE FROM students;";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* del = dynamic_cast<DeleteStatement*>(stmt.get());
    ASSERT_NE(del, nullptr);
    EXPECT_EQ(del->table_name(), "students");
    EXPECT_EQ(del->where_clause(), nullptr);
}

TEST(ParserTest, CreateIndexStatement) {
    std::string sql = "CREATE INDEX idx_student_cgpa ON students (cgpa);";
    auto [stmt, status] = Parser::parse(sql);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(stmt, nullptr);

    auto* idx = dynamic_cast<CreateIndexStatement*>(stmt.get());
    ASSERT_NE(idx, nullptr);
    EXPECT_EQ(idx->index_name(), "idx_student_cgpa");
    EXPECT_EQ(idx->table_name(), "students");
    ASSERT_EQ(idx->columns().size(), 1);
    EXPECT_EQ(idx->columns()[0], "cgpa");
}

TEST(ParserTest, ComparisonExpressions) {
    std::vector<std::pair<std::string, BinaryOp>> tests = {
        {"x = 1", BinaryOp::Equal},
        {"x != 1", BinaryOp::NotEqual},
        {"x <> 1", BinaryOp::NotEqual},
        {"x < 1", BinaryOp::LessThan},
        {"x <= 1", BinaryOp::LessEqual},
        {"x > 1", BinaryOp::GreaterThan},
        {"x >= 1", BinaryOp::GreaterEqual}
    };

    for (const auto& [expr_str, expected_op] : tests) {
        Parser parser(expr_str);
        auto expr = parser.parse_expression();
        ASSERT_TRUE(parser.status().ok()) << "Failed on: " << expr_str;
        ASSERT_NE(expr, nullptr);
        ASSERT_EQ(expr->expr_type(), ExpressionType::BINARY);
        const auto* bin = dynamic_cast<const BinaryExpression*>(expr.get());
        EXPECT_EQ(bin->op(), expected_op);
    }
}

TEST(ParserTest, AndOrPrecedence) {
    // AND has higher precedence than OR:
    // a > 9.0 OR b < 6.0 AND c = 10
    // => OR( (a > 9.0), AND( (b < 6.0), (c = 10) ) )
    std::string sql = "a > 9.0 OR b < 6.0 AND c = 10";
    Parser parser(sql);
    auto expr = parser.parse_expression();

    ASSERT_TRUE(parser.status().ok()) << parser.status().message();
    ASSERT_NE(expr, nullptr);
    ASSERT_EQ(expr->expr_type(), ExpressionType::BINARY);

    const auto* top_bin = dynamic_cast<const BinaryExpression*>(expr.get());
    ASSERT_NE(top_bin, nullptr);
    EXPECT_EQ(top_bin->op(), BinaryOp::Or);

    EXPECT_EQ(top_bin->left()->to_string(), "(a > 9.0)");

    ASSERT_EQ(top_bin->right()->expr_type(), ExpressionType::BINARY);
    const auto* right_bin = dynamic_cast<const BinaryExpression*>(top_bin->right());
    EXPECT_EQ(right_bin->op(), BinaryOp::And);
    EXPECT_EQ(right_bin->left()->to_string(), "(b < 6.0)");
    EXPECT_EQ(right_bin->right()->to_string(), "(c = 10)");
}

TEST(ParserTest, ArithmeticPrecedence) {
    // 1 + 2 * 3 => 1 + (2 * 3)
    std::string sql = "1 + 2 * 3";
    Parser parser(sql);
    auto expr = parser.parse_expression();

    ASSERT_TRUE(parser.status().ok()) << parser.status().message();
    ASSERT_NE(expr, nullptr);

    const auto* top = dynamic_cast<const BinaryExpression*>(expr.get());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->op(), BinaryOp::Add);
    EXPECT_EQ(top->left()->to_string(), "1");

    const auto* right = dynamic_cast<const BinaryExpression*>(top->right());
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->op(), BinaryOp::Multiply);
    EXPECT_EQ(right->left()->to_string(), "2");
    EXPECT_EQ(right->right()->to_string(), "3");
}

TEST(ParserTest, ArithmeticLeftAssociativity) {
    // 10 - 4 - 2 => (10 - 4) - 2
    std::string sql = "10 - 4 - 2";
    Parser parser(sql);
    auto expr = parser.parse_expression();

    ASSERT_TRUE(parser.status().ok());
    ASSERT_NE(expr, nullptr);

    const auto* top = dynamic_cast<const BinaryExpression*>(expr.get());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->op(), BinaryOp::Subtract);
    EXPECT_EQ(top->right()->to_string(), "2");

    const auto* left = dynamic_cast<const BinaryExpression*>(top->left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->op(), BinaryOp::Subtract);
    EXPECT_EQ(left->left()->to_string(), "10");
    EXPECT_EQ(left->right()->to_string(), "4");
}

TEST(ParserTest, ParenthesizedExpressions) {
    // (1 + 2) * 3 => ((1 + 2) * 3)
    std::string sql = "(1 + 2) * 3";
    Parser parser(sql);
    auto expr = parser.parse_expression();

    ASSERT_TRUE(parser.status().ok());
    ASSERT_NE(expr, nullptr);

    const auto* top = dynamic_cast<const BinaryExpression*>(expr.get());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->op(), BinaryOp::Multiply);
    EXPECT_EQ(top->right()->to_string(), "3");

    const auto* left = dynamic_cast<const BinaryExpression*>(top->left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->op(), BinaryOp::Add);
    EXPECT_EQ(left->left()->to_string(), "1");
    EXPECT_EQ(left->right()->to_string(), "2");
}

TEST(ParserTest, NestedComplexExpression) {
    std::string sql = "NOT (cgpa <= 5.0) AND (dept = 'CS' OR dept = 'ECE')";
    Parser parser(sql);
    auto expr = parser.parse_expression();

    ASSERT_TRUE(parser.status().ok()) << parser.status().message();
    ASSERT_NE(expr, nullptr);

    const auto* top = dynamic_cast<const BinaryExpression*>(expr.get());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->op(), BinaryOp::And);

    // Left child is NOT (cgpa <= 5.0)
    ASSERT_EQ(top->left()->expr_type(), ExpressionType::UNARY);
    const auto* left_un = dynamic_cast<const UnaryExpression*>(top->left());
    EXPECT_EQ(left_un->op(), UnaryOp::Not);

    // Right child is (dept = 'CS' OR dept = 'ECE')
    ASSERT_EQ(top->right()->expr_type(), ExpressionType::BINARY);
    const auto* right_bin = dynamic_cast<const BinaryExpression*>(top->right());
    EXPECT_EQ(right_bin->op(), BinaryOp::Or);
}

TEST(ParserTest, ErrorMissingSelectColumns) {
    std::string sql = "SELECT FROM students;";
    auto [stmt, status] = Parser::parse(sql);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), StatusCode::SyntaxError);
    EXPECT_EQ(stmt, nullptr);
}

TEST(ParserTest, ErrorMissingFromClause) {
    std::string sql = "SELECT name students;";
    auto [stmt, status] = Parser::parse(sql);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), StatusCode::SyntaxError);
    EXPECT_EQ(stmt, nullptr);
}

TEST(ParserTest, ErrorMissingParentheses) {
    std::string sql = "SELECT * FROM students WHERE (cgpa > 9.0;";
    auto [stmt, status] = Parser::parse(sql);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), StatusCode::SyntaxError);
    EXPECT_EQ(stmt, nullptr);
}

TEST(ParserTest, ErrorUnexpectedTokens) {
    std::string sql = "SELECT * FROM students WHERE cgpa > 9.0 EXTRA_GARBAGE;";
    auto [stmt, status] = Parser::parse(sql);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), StatusCode::SyntaxError);
    EXPECT_EQ(stmt, nullptr);
}

TEST(ParserTest, ErrorInvalidCreateTable) {
    std::string sql = "CREATE TABLE users (id INVALID_TYPE);";
    auto [stmt, status] = Parser::parse(sql);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), StatusCode::SyntaxError);
    EXPECT_EQ(stmt, nullptr);
}

TEST(ParserTest, EmptyInputGracefullyHandled) {
    {
        auto [stmt, status] = Parser::parse("");
        EXPECT_TRUE(status.ok());
        EXPECT_EQ(stmt, nullptr);
    }
    {
        auto [stmt, status] = Parser::parse("   \t\n  ");
        EXPECT_TRUE(status.ok());
        EXPECT_EQ(stmt, nullptr);
    }
}

TEST(ParserTest, MultipleStatementsParsing) {
    std::string sql =
        "CREATE TABLE t (id INT); "
        "INSERT INTO t VALUES (1); "
        "SELECT * FROM t;";
    Parser parser(sql);
    auto stmts = parser.parse_statements();

    ASSERT_TRUE(parser.status().ok()) << parser.status().message();
    ASSERT_EQ(stmts.size(), 3);
    EXPECT_EQ(stmts[0]->stmt_type(), StatementType::CREATE_TABLE);
    EXPECT_EQ(stmts[1]->stmt_type(), StatementType::INSERT);
    EXPECT_EQ(stmts[2]->stmt_type(), StatementType::SELECT);
}
