#include "execution/expression_evaluator.hpp"

#include <cmath>

namespace querycraft::execution {

common::Status ExpressionEvaluator::evaluate(const parser::Expression* expr,
                                            const storage::Record* record,
                                            const storage::Schema* schema,
                                            storage::Value& out_value) const {
    if (expr == nullptr) {
        out_value = storage::Value{};
        return common::Status::Ok();
    }

    switch (expr->expr_type()) {
        case parser::ExpressionType::LITERAL: {
            const auto* lit = dynamic_cast<const parser::LiteralExpression*>(expr);
            return evaluate_literal(lit, out_value);
        }
        case parser::ExpressionType::COLUMN_REF: {
            const auto* col_ref = dynamic_cast<const parser::ColumnRefExpression*>(expr);
            return evaluate_column_ref(col_ref, record, schema, out_value);
        }
        case parser::ExpressionType::UNARY: {
            const auto* un = dynamic_cast<const parser::UnaryExpression*>(expr);
            return evaluate_unary(un, record, schema, out_value);
        }
        case parser::ExpressionType::BINARY: {
            const auto* bin = dynamic_cast<const parser::BinaryExpression*>(expr);
            return evaluate_binary(bin, record, schema, out_value);
        }
    }

    return common::Status::NotSupported("Unsupported expression type in evaluator");
}

common::Status ExpressionEvaluator::evaluate_predicate(const parser::Expression* expr,
                                                      const storage::Record& record,
                                                      const storage::Schema& schema,
                                                      bool& out_matches) const {
    out_matches = false;
    if (expr == nullptr) {
        out_matches = true;
        return common::Status::Ok();
    }

    storage::Value val;
    const auto st = evaluate(expr, &record, &schema, val);
    if (!st.ok()) {
        return st;
    }

    // In SQL 3-valued logic, NULL evaluates to false in WHERE filtering
    if (val.is_null()) {
        out_matches = false;
        return common::Status::Ok();
    }

    if (val.is_int()) {
        out_matches = (val.as_int() != 0);
        return common::Status::Ok();
    }

    return common::Status::InvalidArgument("WHERE clause condition must evaluate to a boolean or integer");
}

common::Status ExpressionEvaluator::evaluate_constant(const parser::Expression* expr,
                                                     storage::Value& out_value) const {
    return evaluate(expr, nullptr, nullptr, out_value);
}

common::Status ExpressionEvaluator::evaluate_literal(const parser::LiteralExpression* expr,
                                                    storage::Value& out_value) const {
    if (expr == nullptr || expr->is_null()) {
        out_value = storage::Value{};
        return common::Status::Ok();
    }

    if (expr->is_int()) {
        out_value = storage::Value(expr->int_value());
        return common::Status::Ok();
    }

    if (expr->is_float()) {
        out_value = storage::Value(expr->float_value());
        return common::Status::Ok();
    }

    if (expr->is_string()) {
        out_value = storage::Value(expr->string_value());
        return common::Status::Ok();
    }

    if (expr->is_bool()) {
        out_value = storage::Value(static_cast<std::int64_t>(expr->bool_value() ? 1 : 0));
        return common::Status::Ok();
    }

    return common::Status::InvalidArgument("Unknown literal type in expression");
}

common::Status ExpressionEvaluator::evaluate_column_ref(const parser::ColumnRefExpression* expr,
                                                       const storage::Record* record,
                                                       const storage::Schema* schema,
                                                       storage::Value& out_value) const {
    if (expr == nullptr) {
        return common::Status::InvalidArgument("Null column reference");
    }

    if (record == nullptr || schema == nullptr) {
        return common::Status::InvalidArgument(
            "Column reference '" + expr->column_name() + "' cannot be evaluated in constant context");
    }

    const auto col_idx = schema->get_column_index(expr->column_name());
    if (!col_idx.has_value()) {
        return common::Status::NotFound(
            "Column '" + expr->column_name() + "' does not exist in table schema");
    }

    out_value = record->get_value(*col_idx);
    return common::Status::Ok();
}

common::Status ExpressionEvaluator::evaluate_unary(const parser::UnaryExpression* expr,
                                                  const storage::Record* record,
                                                  const storage::Schema* schema,
                                                  storage::Value& out_value) const {
    if (expr == nullptr || expr->operand() == nullptr) {
        return common::Status::InvalidArgument("Invalid unary expression");
    }

    storage::Value operand_val;
    const auto st = evaluate(expr->operand(), record, schema, operand_val);
    if (!st.ok()) {
        return st;
    }

    // SQL NULL unary operations propagate NULL
    if (operand_val.is_null()) {
        out_value = storage::Value{};
        return common::Status::Ok();
    }

    switch (expr->op()) {
        case parser::UnaryOp::Plus: {
            if (operand_val.is_int() || operand_val.is_float()) {
                out_value = operand_val;
                return common::Status::Ok();
            }
            return common::Status::InvalidArgument("Unary '+' requires numeric operand");
        }
        case parser::UnaryOp::Minus: {
            if (operand_val.is_int()) {
                out_value = storage::Value(-operand_val.as_int());
                return common::Status::Ok();
            }
            if (operand_val.is_float()) {
                out_value = storage::Value(-operand_val.as_float());
                return common::Status::Ok();
            }
            return common::Status::InvalidArgument("Unary '-' requires numeric operand");
        }
        case parser::UnaryOp::Not: {
            if (operand_val.is_int()) {
                out_value = storage::Value(static_cast<std::int64_t>(operand_val.as_int() == 0 ? 1 : 0));
                return common::Status::Ok();
            }
            return common::Status::InvalidArgument("Unary 'NOT' requires boolean/integer operand");
        }
    }

    return common::Status::NotSupported("Unsupported unary operator");
}

common::Status ExpressionEvaluator::evaluate_binary(const parser::BinaryExpression* expr,
                                                   const storage::Record* record,
                                                   const storage::Schema* schema,
                                                   storage::Value& out_value) const {
    if (expr == nullptr || expr->left() == nullptr || expr->right() == nullptr) {
        return common::Status::InvalidArgument("Invalid binary expression");
    }

    // Handle short-circuiting logical AND and OR operators
    if (expr->op() == parser::BinaryOp::And) {
        return evaluate_logical_and(expr->left(), expr->right(), record, schema, out_value);
    }
    if (expr->op() == parser::BinaryOp::Or) {
        return evaluate_logical_or(expr->left(), expr->right(), record, schema, out_value);
    }

    storage::Value left_val;
    auto st = evaluate(expr->left(), record, schema, left_val);
    if (!st.ok()) {
        return st;
    }

    storage::Value right_val;
    st = evaluate(expr->right(), record, schema, right_val);
    if (!st.ok()) {
        return st;
    }

    switch (expr->op()) {
        case parser::BinaryOp::Add:
        case parser::BinaryOp::Subtract:
        case parser::BinaryOp::Multiply:
        case parser::BinaryOp::Divide:
            return evaluate_arithmetic(expr->op(), left_val, right_val, out_value);

        case parser::BinaryOp::Equal:
        case parser::BinaryOp::NotEqual:
        case parser::BinaryOp::LessThan:
        case parser::BinaryOp::LessEqual:
        case parser::BinaryOp::GreaterThan:
        case parser::BinaryOp::GreaterEqual:
            return evaluate_comparison(expr->op(), left_val, right_val, out_value);

        case parser::BinaryOp::And:
        case parser::BinaryOp::Or:
            break; // Handled above
    }

    return common::Status::NotSupported("Unsupported binary operator");
}

common::Status ExpressionEvaluator::evaluate_arithmetic(parser::BinaryOp op,
                                                       const storage::Value& left,
                                                       const storage::Value& right,
                                                       storage::Value& out_value) const {
    // In SQL, arithmetic with NULL evaluates to NULL
    if (left.is_null() || right.is_null()) {
        out_value = storage::Value{};
        return common::Status::Ok();
    }

    if (left.is_string() || right.is_string()) {
        return common::Status::InvalidArgument("Arithmetic operations (+, -, *, /) require numeric operands");
    }

    // Both operands are integers -> 64-bit integer arithmetic
    if (left.is_int() && right.is_int()) {
        const auto l = left.as_int();
        const auto r = right.as_int();

        if (op == parser::BinaryOp::Divide) {
            if (r == 0) {
                return common::Status::InvalidArgument("Division by zero");
            }
            out_value = storage::Value(l / r);
            return common::Status::Ok();
        }

        switch (op) {
            case parser::BinaryOp::Add:      out_value = storage::Value(l + r); return common::Status::Ok();
            case parser::BinaryOp::Subtract: out_value = storage::Value(l - r); return common::Status::Ok();
            case parser::BinaryOp::Multiply: out_value = storage::Value(l * r); return common::Status::Ok();
            default: break;
        }
    }

    // At least one operand is floating-point -> promote to double
    if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        const double l = left.is_int() ? static_cast<double>(left.as_int()) : left.as_float();
        const double r = right.is_int() ? static_cast<double>(right.as_int()) : right.as_float();

        if (op == parser::BinaryOp::Divide) {
            if (std::abs(r) < 1e-12) {
                return common::Status::InvalidArgument("Division by zero");
            }
            out_value = storage::Value(l / r);
            return common::Status::Ok();
        }

        switch (op) {
            case parser::BinaryOp::Add:      out_value = storage::Value(l + r); return common::Status::Ok();
            case parser::BinaryOp::Subtract: out_value = storage::Value(l - r); return common::Status::Ok();
            case parser::BinaryOp::Multiply: out_value = storage::Value(l * r); return common::Status::Ok();
            default: break;
        }
    }

    return common::Status::InvalidArgument("Type mismatch in arithmetic operation");
}

common::Status ExpressionEvaluator::evaluate_comparison(parser::BinaryOp op,
                                                        const storage::Value& left,
                                                        const storage::Value& right,
                                                        storage::Value& out_value) const {
    // In SQL 3-valued logic, comparison with NULL yields NULL
    if (left.is_null() || right.is_null()) {
        out_value = storage::Value{};
        return common::Status::Ok();
    }

    int cmp = 0;

    if (left.is_int() && right.is_int()) {
        const auto l = left.as_int();
        const auto r = right.as_int();
        cmp = (l < r) ? -1 : (l > r ? 1 : 0);
    } else if (left.is_float() && right.is_float()) {
        const auto l = left.as_float();
        const auto r = right.as_float();
        cmp = (l < r) ? -1 : (l > r ? 1 : 0);
    } else if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
        const double l = left.is_int() ? static_cast<double>(left.as_int()) : left.as_float();
        const double r = right.is_int() ? static_cast<double>(right.as_int()) : right.as_float();
        cmp = (l < r) ? -1 : (l > r ? 1 : 0);
    } else if (left.is_string() && right.is_string()) {
        const auto& l = left.as_string();
        const auto& r = right.as_string();
        cmp = (l < r) ? -1 : (l > r ? 1 : 0);
    } else {
        return common::Status::InvalidArgument("Cannot compare incompatible types: string and numeric");
    }

    bool matched = false;
    switch (op) {
        case parser::BinaryOp::Equal:        matched = (cmp == 0); break;
        case parser::BinaryOp::NotEqual:     matched = (cmp != 0); break;
        case parser::BinaryOp::LessThan:     matched = (cmp < 0);  break;
        case parser::BinaryOp::LessEqual:    matched = (cmp <= 0); break;
        case parser::BinaryOp::GreaterThan:  matched = (cmp > 0);  break;
        case parser::BinaryOp::GreaterEqual: matched = (cmp >= 0); break;
        default: break;
    }

    out_value = storage::Value(static_cast<std::int64_t>(matched ? 1 : 0));
    return common::Status::Ok();
}

common::Status ExpressionEvaluator::evaluate_logical_and(const parser::Expression* left_expr,
                                                         const parser::Expression* right_expr,
                                                         const storage::Record* record,
                                                         const storage::Schema* schema,
                                                         storage::Value& out_value) const {
    storage::Value left_val;
    auto st = evaluate(left_expr, record, schema, left_val);
    if (!st.ok()) {
        return st;
    }

    // Short-circuit: false AND anything -> false
    if (!left_val.is_null() && left_val.is_int() && left_val.as_int() == 0) {
        out_value = storage::Value(static_cast<std::int64_t>(0));
        return common::Status::Ok();
    }

    storage::Value right_val;
    st = evaluate(right_expr, record, schema, right_val);
    if (!st.ok()) {
        return st;
    }

    // false AND anything -> false
    if (!right_val.is_null() && right_val.is_int() && right_val.as_int() == 0) {
        out_value = storage::Value(static_cast<std::int64_t>(0));
        return common::Status::Ok();
    }

    // If either side is NULL: result is NULL
    if (left_val.is_null() || right_val.is_null()) {
        out_value = storage::Value{};
        return common::Status::Ok();
    }

    if (!left_val.is_int() || !right_val.is_int()) {
        return common::Status::InvalidArgument("Operands to 'AND' must be boolean/integer");
    }

    const bool b = (left_val.as_int() != 0) && (right_val.as_int() != 0);
    out_value = storage::Value(static_cast<std::int64_t>(b ? 1 : 0));
    return common::Status::Ok();
}

common::Status ExpressionEvaluator::evaluate_logical_or(const parser::Expression* left_expr,
                                                        const parser::Expression* right_expr,
                                                        const storage::Record* record,
                                                        const storage::Schema* schema,
                                                        storage::Value& out_value) const {
    storage::Value left_val;
    auto st = evaluate(left_expr, record, schema, left_val);
    if (!st.ok()) {
        return st;
    }

    // Short-circuit: true OR anything -> true
    if (!left_val.is_null() && left_val.is_int() && left_val.as_int() != 0) {
        out_value = storage::Value(static_cast<std::int64_t>(1));
        return common::Status::Ok();
    }

    storage::Value right_val;
    st = evaluate(right_expr, record, schema, right_val);
    if (!st.ok()) {
        return st;
    }

    // anything OR true -> true
    if (!right_val.is_null() && right_val.is_int() && right_val.as_int() != 0) {
        out_value = storage::Value(static_cast<std::int64_t>(1));
        return common::Status::Ok();
    }

    // If either side is NULL: result is NULL
    if (left_val.is_null() || right_val.is_null()) {
        out_value = storage::Value{};
        return common::Status::Ok();
    }

    if (!left_val.is_int() || !right_val.is_int()) {
        return common::Status::InvalidArgument("Operands to 'OR' must be boolean/integer");
    }

    const bool b = (left_val.as_int() != 0) || (right_val.as_int() != 0);
    out_value = storage::Value(static_cast<std::int64_t>(b ? 1 : 0));
    return common::Status::Ok();
}

} // namespace querycraft::execution
