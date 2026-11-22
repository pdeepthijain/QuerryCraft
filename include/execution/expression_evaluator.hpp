#pragma once

#include "common/status.hpp"
#include "parser/ast.hpp"
#include "storage/record.hpp"
#include "storage/schema.hpp"

#include <utility>

namespace querycraft::execution {

class ExpressionEvaluator {
public:
    ExpressionEvaluator() = default;

    // Evaluates an expression against a record and its schema
    common::Status evaluate(const parser::Expression* expr,
                            const storage::Record* record,
                            const storage::Schema* schema,
                            storage::Value& out_value) const;

    // Evaluates an expression as a boolean predicate for WHERE clause filtering.
    // In SQL 3-valued logic, NULL evaluates to false for WHERE filtering.
    common::Status evaluate_predicate(const parser::Expression* expr,
                                      const storage::Record& record,
                                      const storage::Schema& schema,
                                      bool& out_matches) const;

    // Evaluates a constant expression (e.g. in INSERT VALUES) without a record/schema
    common::Status evaluate_constant(const parser::Expression* expr,
                                     storage::Value& out_value) const;

private:
    common::Status evaluate_literal(const parser::LiteralExpression* expr,
                                    storage::Value& out_value) const;

    common::Status evaluate_column_ref(const parser::ColumnRefExpression* expr,
                                       const storage::Record* record,
                                       const storage::Schema* schema,
                                       storage::Value& out_value) const;

    common::Status evaluate_unary(const parser::UnaryExpression* expr,
                                  const storage::Record* record,
                                  const storage::Schema* schema,
                                  storage::Value& out_value) const;

    common::Status evaluate_binary(const parser::BinaryExpression* expr,
                                   const storage::Record* record,
                                   const storage::Schema* schema,
                                   storage::Value& out_value) const;

    // Sub-evaluators for binary operators
    common::Status evaluate_arithmetic(parser::BinaryOp op,
                                       const storage::Value& left,
                                       const storage::Value& right,
                                       storage::Value& out_value) const;

    common::Status evaluate_comparison(parser::BinaryOp op,
                                       const storage::Value& left,
                                       const storage::Value& right,
                                       storage::Value& out_value) const;

    common::Status evaluate_logical_and(const parser::Expression* left_expr,
                                        const parser::Expression* right_expr,
                                        const storage::Record* record,
                                        const storage::Schema* schema,
                                        storage::Value& out_value) const;

    common::Status evaluate_logical_or(const parser::Expression* left_expr,
                                       const parser::Expression* right_expr,
                                       const storage::Record* record,
                                       const storage::Schema* schema,
                                       storage::Value& out_value) const;
};

} // namespace querycraft::execution
