#include <gtest/gtest.h>

#include "execution/expression_evaluator.hpp"
#include "parser/parser.hpp"

namespace querycraft::execution {

class ExpressionEvaluatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        schema_ = storage::Schema({
            storage::Column("id", storage::DataType::INT, /*is_primary_key=*/true),
            storage::Column("name", storage::DataType::VARCHAR, false, true, 50),
            storage::Column("gpa", storage::DataType::FLOAT, false, true),
            storage::Column("age", storage::DataType::INT, false, true)
        });

        record_ = storage::Record({
            storage::Value(static_cast<std::int64_t>(10)),
            storage::Value(std::string("Alice")),
            storage::Value(3.85),
            storage::Value(static_cast<std::int64_t>(21))
        });
    }

    std::unique_ptr<parser::Expression> parse_expr(std::string_view sql) {
        parser::Parser p(sql);
        auto expr = p.parse_expression();
        EXPECT_FALSE(p.has_error()) << "Failed to parse expression: " << sql << " Error: " << p.status().message();
        return expr;
    }

    storage::Schema schema_;
    storage::Record record_;
    ExpressionEvaluator evaluator_;
};

TEST_F(ExpressionEvaluatorTest, LiteralEvaluation) {
    storage::Value val;

    auto e1 = parse_expr("42");
    ASSERT_TRUE(evaluator_.evaluate_constant(e1.get(), val).ok());
    EXPECT_TRUE(val.is_int());
    EXPECT_EQ(val.as_int(), 42);

    auto e2 = parse_expr("3.14159");
    ASSERT_TRUE(evaluator_.evaluate_constant(e2.get(), val).ok());
    EXPECT_TRUE(val.is_float());
    EXPECT_DOUBLE_EQ(val.as_float(), 3.14159);

    auto e3 = parse_expr("'QueryCraft'");
    ASSERT_TRUE(evaluator_.evaluate_constant(e3.get(), val).ok());
    EXPECT_TRUE(val.is_string());
    EXPECT_EQ(val.as_string(), "QueryCraft");

    auto e4 = parse_expr("NULL");
    ASSERT_TRUE(evaluator_.evaluate_constant(e4.get(), val).ok());
    EXPECT_TRUE(val.is_null());
}

TEST_F(ExpressionEvaluatorTest, ColumnReferenceEvaluation) {
    storage::Value val;

    auto e1 = parse_expr("id");
    ASSERT_TRUE(evaluator_.evaluate(e1.get(), &record_, &schema_, val).ok());
    EXPECT_EQ(val.as_int(), 10);

    auto e2 = parse_expr("name");
    ASSERT_TRUE(evaluator_.evaluate(e2.get(), &record_, &schema_, val).ok());
    EXPECT_EQ(val.as_string(), "Alice");

    auto e3 = parse_expr("gpa");
    ASSERT_TRUE(evaluator_.evaluate(e3.get(), &record_, &schema_, val).ok());
    EXPECT_DOUBLE_EQ(val.as_float(), 3.85);

    // Non-existent column
    auto e4 = parse_expr("non_existent_column");
    auto st = evaluator_.evaluate(e4.get(), &record_, &schema_, val);
    EXPECT_FALSE(st.ok());
    EXPECT_EQ(st.code(), common::StatusCode::NotFound);

    // Column ref in constant context must fail
    auto e5 = parse_expr("id");
    EXPECT_FALSE(evaluator_.evaluate_constant(e5.get(), val).ok());
}

TEST_F(ExpressionEvaluatorTest, ArithmeticOperations) {
    storage::Value val;

    // Integer + Integer
    auto e1 = parse_expr("10 + 25");
    ASSERT_TRUE(evaluator_.evaluate_constant(e1.get(), val).ok());
    EXPECT_EQ(val.as_int(), 35);

    // Integer - Integer
    auto e2 = parse_expr("50 - 15");
    ASSERT_TRUE(evaluator_.evaluate_constant(e2.get(), val).ok());
    EXPECT_EQ(val.as_int(), 35);

    // Integer * Integer
    auto e3 = parse_expr("6 * 7");
    ASSERT_TRUE(evaluator_.evaluate_constant(e3.get(), val).ok());
    EXPECT_EQ(val.as_int(), 42);

    // Integer / Integer
    auto e4 = parse_expr("100 / 4");
    ASSERT_TRUE(evaluator_.evaluate_constant(e4.get(), val).ok());
    EXPECT_EQ(val.as_int(), 25);

    // Float + Integer promotion
    auto e5 = parse_expr("2.5 + 4");
    ASSERT_TRUE(evaluator_.evaluate_constant(e5.get(), val).ok());
    EXPECT_TRUE(val.is_float());
    EXPECT_DOUBLE_EQ(val.as_float(), 6.5);

    // Division by zero
    auto e6 = parse_expr("10 / 0");
    auto st = evaluator_.evaluate_constant(e6.get(), val);
    EXPECT_FALSE(st.ok());
    EXPECT_EQ(st.code(), common::StatusCode::InvalidArgument);

    // Unary minus
    auto e7 = parse_expr("-42");
    ASSERT_TRUE(evaluator_.evaluate_constant(e7.get(), val).ok());
    EXPECT_EQ(val.as_int(), -42);
}

TEST_F(ExpressionEvaluatorTest, ComparisonOperations) {
    storage::Value val;

    // Equality
    auto e1 = parse_expr("10 = 10");
    ASSERT_TRUE(evaluator_.evaluate_constant(e1.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    auto e2 = parse_expr("10 = 20");
    ASSERT_TRUE(evaluator_.evaluate_constant(e2.get(), val).ok());
    EXPECT_EQ(val.as_int(), 0);

    // Not equal
    auto e3 = parse_expr("10 != 20");
    ASSERT_TRUE(evaluator_.evaluate_constant(e3.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    // Less / Greater
    auto e4 = parse_expr("5 < 10");
    ASSERT_TRUE(evaluator_.evaluate_constant(e4.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    auto e5 = parse_expr("10 <= 10");
    ASSERT_TRUE(evaluator_.evaluate_constant(e5.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    auto e6 = parse_expr("15 > 20");
    ASSERT_TRUE(evaluator_.evaluate_constant(e6.get(), val).ok());
    EXPECT_EQ(val.as_int(), 0);

    // String comparison
    auto e7 = parse_expr("'Alice' = 'Alice'");
    ASSERT_TRUE(evaluator_.evaluate_constant(e7.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    auto e8 = parse_expr("'Alice' < 'Bob'");
    ASSERT_TRUE(evaluator_.evaluate_constant(e8.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    // Incompatible comparison (string vs integer)
    auto e9 = parse_expr("'Alice' = 10");
    auto st = evaluator_.evaluate_constant(e9.get(), val);
    EXPECT_FALSE(st.ok());
}

TEST_F(ExpressionEvaluatorTest, LogicalAndOrNotOperations) {
    storage::Value val;

    // AND
    auto e1 = parse_expr("1 AND 1");
    ASSERT_TRUE(evaluator_.evaluate_constant(e1.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    auto e2 = parse_expr("1 AND 0");
    ASSERT_TRUE(evaluator_.evaluate_constant(e2.get(), val).ok());
    EXPECT_EQ(val.as_int(), 0);

    // OR
    auto e3 = parse_expr("0 OR 1");
    ASSERT_TRUE(evaluator_.evaluate_constant(e3.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    auto e4 = parse_expr("0 OR 0");
    ASSERT_TRUE(evaluator_.evaluate_constant(e4.get(), val).ok());
    EXPECT_EQ(val.as_int(), 0);

    // NOT
    auto e5 = parse_expr("NOT 0");
    ASSERT_TRUE(evaluator_.evaluate_constant(e5.get(), val).ok());
    EXPECT_EQ(val.as_int(), 1);

    auto e6 = parse_expr("NOT 1");
    ASSERT_TRUE(evaluator_.evaluate_constant(e6.get(), val).ok());
    EXPECT_EQ(val.as_int(), 0);
}

TEST_F(ExpressionEvaluatorTest, SqlNullSemantics) {
    storage::Value val;

    // Arithmetic with NULL yields NULL
    auto e1 = parse_expr("10 + NULL");
    ASSERT_TRUE(evaluator_.evaluate_constant(e1.get(), val).ok());
    EXPECT_TRUE(val.is_null());

    auto e2 = parse_expr("NULL * 5.0");
    ASSERT_TRUE(evaluator_.evaluate_constant(e2.get(), val).ok());
    EXPECT_TRUE(val.is_null());

    // Comparison with NULL yields NULL (unknown)
    auto e3 = parse_expr("10 = NULL");
    ASSERT_TRUE(evaluator_.evaluate_constant(e3.get(), val).ok());
    EXPECT_TRUE(val.is_null());

    auto e4 = parse_expr("NULL != NULL");
    ASSERT_TRUE(evaluator_.evaluate_constant(e4.get(), val).ok());
    EXPECT_TRUE(val.is_null());

    // In WHERE predicate evaluation, NULL evaluates to false
    bool matches = false;
    ASSERT_TRUE(evaluator_.evaluate_predicate(e3.get(), record_, schema_, matches).ok());
    EXPECT_FALSE(matches);
}

TEST_F(ExpressionEvaluatorTest, RecordPredicateEvaluation) {
    bool matches = false;

    // age > 18 AND gpa >= 3.5 -> true (21 > 18 and 3.85 >= 3.5)
    auto e1 = parse_expr("age > 18 AND gpa >= 3.5");
    ASSERT_TRUE(evaluator_.evaluate_predicate(e1.get(), record_, schema_, matches).ok());
    EXPECT_TRUE(matches);

    // age < 20 -> false (21 < 20 is false)
    auto e2 = parse_expr("age < 20");
    ASSERT_TRUE(evaluator_.evaluate_predicate(e2.get(), record_, schema_, matches).ok());
    EXPECT_FALSE(matches);

    // name = 'Alice' OR id = 999 -> true
    auto e3 = parse_expr("name = 'Alice' OR id = 999");
    ASSERT_TRUE(evaluator_.evaluate_predicate(e3.get(), record_, schema_, matches).ok());
    EXPECT_TRUE(matches);

    // id + 5 = 15 -> true
    auto e4 = parse_expr("id + 5 = 15");
    ASSERT_TRUE(evaluator_.evaluate_predicate(e4.get(), record_, schema_, matches).ok());
    EXPECT_TRUE(matches);
}

} // namespace querycraft::execution
