#include <gtest/gtest.h>
#include "parser/ast.hpp"

using namespace querycraft::parser;

TEST(AstTest, LiteralExpressionInt) {
    auto lit = LiteralExpression::make_int(42, {1, 5});
    EXPECT_TRUE(lit->is_int());
    EXPECT_FALSE(lit->is_null());
    EXPECT_FALSE(lit->is_float());
    EXPECT_EQ(lit->int_value(), 42);
    EXPECT_EQ(lit->to_string(), "42");
    EXPECT_EQ(lit->line(), 1);
    EXPECT_EQ(lit->column(), 5);

    auto cloned = lit->clone();
    EXPECT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->to_string(), "42");
}

TEST(AstTest, LiteralExpressionFloat) {
    auto lit = LiteralExpression::make_float(9.5, {2, 10});
    EXPECT_TRUE(lit->is_float());
    EXPECT_DOUBLE_EQ(lit->float_value(), 9.5);
    EXPECT_EQ(lit->to_string(), "9.5");

    auto cloned = lit->clone();
    EXPECT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->to_string(), "9.5");
}

TEST(AstTest, LiteralExpressionString) {
    auto lit = LiteralExpression::make_string("Alice", {3, 1});
    EXPECT_TRUE(lit->is_string());
    EXPECT_EQ(lit->string_value(), "Alice");
    EXPECT_EQ(lit->to_string(), "'Alice'");

    auto cloned = lit->clone();
    EXPECT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->to_string(), "'Alice'");
}

TEST(AstTest, LiteralExpressionNull) {
    auto lit = LiteralExpression::make_null({1, 1});
    EXPECT_TRUE(lit->is_null());
    EXPECT_FALSE(lit->is_int());
    EXPECT_EQ(lit->to_string(), "NULL");
}

TEST(AstTest, ColumnRefExpression) {
    ColumnRefExpression simple("cgpa");
    EXPECT_EQ(simple.column_name(), "cgpa");
    EXPECT_FALSE(simple.has_table_name());
    EXPECT_EQ(simple.to_string(), "cgpa");

    ColumnRefExpression qualified("cgpa", "students");
    EXPECT_EQ(qualified.column_name(), "cgpa");
    EXPECT_TRUE(qualified.has_table_name());
    EXPECT_EQ(qualified.table_name(), "students");
    EXPECT_EQ(qualified.to_string(), "students.cgpa");

    auto cloned = qualified.clone();
    EXPECT_EQ(cloned->to_string(), "students.cgpa");
}

TEST(AstTest, BinaryExpression) {
    auto left = std::make_unique<ColumnRefExpression>("age");
    auto right = LiteralExpression::make_int(18);
    BinaryExpression bin(BinaryOp::GreaterEqual, std::move(left), std::move(right));

    EXPECT_EQ(bin.op(), BinaryOp::GreaterEqual);
    EXPECT_EQ(bin.to_string(), "(age >= 18)");

    auto cloned = bin.clone();
    EXPECT_EQ(cloned->to_string(), "(age >= 18)");
}

TEST(AstTest, UnaryExpression) {
    auto operand = std::make_unique<ColumnRefExpression>("is_active");
    UnaryExpression un(UnaryOp::Not, std::move(operand));

    EXPECT_EQ(un.op(), UnaryOp::Not);
    EXPECT_EQ(un.to_string(), "NOT (is_active)");

    auto cloned = un.clone();
    EXPECT_EQ(cloned->to_string(), "NOT (is_active)");
}

TEST(AstTest, SelectStatementToString) {
    SelectStatement stmt;
    stmt.add_select_item(std::make_unique<ColumnRefExpression>("name"));
    stmt.add_select_item(std::make_unique<ColumnRefExpression>("cgpa"));
    stmt.set_from_table("students");

    auto where_cond = std::make_unique<BinaryExpression>(
        BinaryOp::GreaterThan,
        std::make_unique<ColumnRefExpression>("cgpa"),
        LiteralExpression::make_float(9.0)
    );
    stmt.set_where_clause(std::move(where_cond));
    stmt.add_order_by(std::make_unique<ColumnRefExpression>("cgpa"), false);
    stmt.set_limit(10);

    EXPECT_EQ(stmt.to_string(), "SELECT name, cgpa FROM students WHERE (cgpa > 9.0) ORDER BY cgpa DESC LIMIT 10;");

    std::string debug = ast_debug_print(stmt);
    EXPECT_NE(debug.find("SelectStatement"), std::string::npos);
    EXPECT_NE(debug.find("students"), std::string::npos);
    EXPECT_NE(debug.find("Limit: 10"), std::string::npos);
}

TEST(AstTest, CreateTableStatementToString) {
    CreateTableStatement stmt("students");
    stmt.add_column(ColumnDefinition("id", DataType::INT, true));
    stmt.add_column(ColumnDefinition("name", DataType::VARCHAR, false, 100));
    stmt.add_column(ColumnDefinition("cgpa", DataType::FLOAT));

    EXPECT_EQ(stmt.to_string(), "CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(100), cgpa FLOAT);");
    EXPECT_EQ(stmt.primary_key_columns().size(), 1);
    EXPECT_EQ(stmt.primary_key_columns()[0], "id");

    std::string debug = ast_debug_print(stmt);
    EXPECT_NE(debug.find("CreateTableStatement"), std::string::npos);
    EXPECT_NE(debug.find("TableName: students"), std::string::npos);
}

TEST(AstTest, InsertStatementToString) {
    InsertStatement stmt("students");
    stmt.add_column("id");
    stmt.add_column("name");
    stmt.add_value(LiteralExpression::make_int(1));
    stmt.add_value(LiteralExpression::make_string("Alice"));

    EXPECT_EQ(stmt.to_string(), "INSERT INTO students (id, name) VALUES (1, 'Alice');");

    std::string debug = ast_debug_print(stmt);
    EXPECT_NE(debug.find("InsertStatement"), std::string::npos);
    EXPECT_NE(debug.find("Alice"), std::string::npos);
}

TEST(AstTest, UpdateStatementToString) {
    UpdateStatement stmt("students");
    stmt.add_assignment("cgpa", LiteralExpression::make_float(9.9));
    auto where_cond = std::make_unique<BinaryExpression>(
        BinaryOp::Equal,
        std::make_unique<ColumnRefExpression>("id"),
        LiteralExpression::make_int(1)
    );
    stmt.set_where_clause(std::move(where_cond));

    EXPECT_EQ(stmt.to_string(), "UPDATE students SET cgpa = 9.9 WHERE (id = 1);");

    std::string debug = ast_debug_print(stmt);
    EXPECT_NE(debug.find("UpdateStatement"), std::string::npos);
    EXPECT_NE(debug.find("cgpa ="), std::string::npos);
}

TEST(AstTest, DeleteStatementToString) {
    DeleteStatement stmt("students");
    auto where_cond = std::make_unique<BinaryExpression>(
        BinaryOp::LessThan,
        std::make_unique<ColumnRefExpression>("cgpa"),
        LiteralExpression::make_float(5.0)
    );
    stmt.set_where_clause(std::move(where_cond));

    EXPECT_EQ(stmt.to_string(), "DELETE FROM students WHERE (cgpa < 5.0);");

    std::string debug = ast_debug_print(stmt);
    EXPECT_NE(debug.find("DeleteStatement"), std::string::npos);
}

TEST(AstTest, CreateIndexStatementToString) {
    CreateIndexStatement stmt("idx_cgpa", "students");
    stmt.add_column("cgpa");

    EXPECT_EQ(stmt.to_string(), "CREATE INDEX idx_cgpa ON students (cgpa);");

    std::string debug = ast_debug_print(stmt);
    EXPECT_NE(debug.find("CreateIndexStatement"), std::string::npos);
    EXPECT_NE(debug.find("idx_cgpa"), std::string::npos);
}

TEST(AstTest, DebugPrintThreeStatements) {
    // 1. SELECT with WHERE and ORDER BY
    SelectStatement select_stmt;
    select_stmt.add_select_item(std::make_unique<ColumnRefExpression>("name"));
    select_stmt.add_select_item(std::make_unique<ColumnRefExpression>("cgpa"));
    select_stmt.set_from_table("students");
    select_stmt.set_where_clause(std::make_unique<BinaryExpression>(
        BinaryOp::GreaterThan,
        std::make_unique<ColumnRefExpression>("cgpa"),
        LiteralExpression::make_float(9.0)
    ));
    select_stmt.add_order_by(std::make_unique<ColumnRefExpression>("cgpa"), false);
    select_stmt.set_limit(5);

    std::string select_debug = ast_debug_print(select_stmt);
    EXPECT_FALSE(select_debug.empty());
    std::cout << "\n--- [Demo 1: SELECT Statement AST] ---\n" << select_debug;

    // 2. CREATE TABLE with PRIMARY KEY
    CreateTableStatement create_stmt("enrollments");
    create_stmt.add_column(ColumnDefinition("id", DataType::INT, true));
    create_stmt.add_column(ColumnDefinition("student_name", DataType::VARCHAR, false, 100));
    create_stmt.add_column(ColumnDefinition("gpa", DataType::FLOAT));

    std::string create_debug = ast_debug_print(create_stmt);
    EXPECT_FALSE(create_debug.empty());
    std::cout << "\n--- [Demo 2: CREATE TABLE Statement AST] ---\n" << create_debug;

    // 3. INSERT with values
    InsertStatement insert_stmt("students");
    insert_stmt.add_column("id");
    insert_stmt.add_column("name");
    insert_stmt.add_column("cgpa");
    insert_stmt.add_value(LiteralExpression::make_int(1));
    insert_stmt.add_value(LiteralExpression::make_string("Alice"));
    insert_stmt.add_value(LiteralExpression::make_float(9.5));

    std::string insert_debug = ast_debug_print(insert_stmt);
    EXPECT_FALSE(insert_debug.empty());
    std::cout << "\n--- [Demo 3: INSERT Statement AST] ---\n" << insert_debug;
}
