#include "execution/select_executor.hpp"
#include "execution/expression_evaluator.hpp"

#include <algorithm>

namespace querycraft::execution {

common::Status SelectExecutor::execute(ExecutionContext& context, ExecutionResult& out_result) {
    const auto& table_name = stmt_.from_table();
    if (table_name.empty()) {
        auto st = common::Status::InvalidArgument("SELECT requires a FROM table");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    if (!context.database().is_open()) {
        auto st = common::Status::IoError("Database is not open");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    auto* table = context.database().get_table(table_name);
    if (table == nullptr) {
        auto st = common::Status::NotFound("Table '" + table_name + "' does not exist");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    const auto& schema = table->schema();
    ExpressionEvaluator evaluator;

    // Scan all records from table
    std::vector<storage::Record> all_records;
    auto scan_st = context.database().scan_table(table_name, all_records);
    if (!scan_st.ok()) {
        out_result = ExecutionResult::Error(scan_st);
        return scan_st;
    }

    // Filter records using WHERE clause if present
    std::vector<const storage::Record*> filtered_records;
    filtered_records.reserve(all_records.size());

    if (stmt_.where_clause() != nullptr) {
        for (const auto& rec : all_records) {
            bool matches = false;
            auto pred_st = evaluator.evaluate_predicate(stmt_.where_clause(), rec, schema, matches);
            if (!pred_st.ok()) {
                out_result = ExecutionResult::Error(pred_st);
                return pred_st;
            }
            if (matches) {
                filtered_records.push_back(&rec);
            }
        }
    } else {
        for (const auto& rec : all_records) {
            filtered_records.push_back(&rec);
        }
    }

    // Determine projected columns
    std::vector<std::string> col_names;
    std::vector<storage::DataType> col_types;
    std::vector<std::vector<storage::Value>> result_rows;

    const bool select_all = stmt_.select_all() || stmt_.select_list().empty();

    if (select_all) {
        col_names.reserve(schema.column_count());
        col_types.reserve(schema.column_count());
        for (std::size_t i = 0; i < schema.column_count(); ++i) {
            const auto& col = schema.get_column(i);
            col_names.push_back(col.name());
            col_types.push_back(col.type());
        }

        result_rows.reserve(filtered_records.size());
        for (const auto* rec : filtered_records) {
            result_rows.push_back(rec->values());
        }
    } else {
        // Individual column expressions
        col_names.reserve(stmt_.select_list().size());
        col_types.reserve(stmt_.select_list().size());

        for (const auto& expr : stmt_.select_list()) {
            if (expr->expr_type() == parser::ExpressionType::COLUMN_REF) {
                const auto* col_ref = dynamic_cast<const parser::ColumnRefExpression*>(expr.get());
                const auto col_idx = schema.get_column_index(col_ref->column_name());
                if (!col_idx.has_value()) {
                    auto st = common::Status::NotFound(
                        "Column '" + col_ref->column_name() + "' does not exist in table '" + table_name + "'");
                    out_result = ExecutionResult::Error(st);
                    return st;
                }
                const auto& col = schema.get_column(*col_idx);
                col_names.push_back(col.name());
                col_types.push_back(col.type());
            } else {
                col_names.push_back(expr->to_string());
                col_types.push_back(storage::DataType::TEXT);
            }
        }

        result_rows.reserve(filtered_records.size());
        for (const auto* rec : filtered_records) {
            std::vector<storage::Value> row;
            row.reserve(stmt_.select_list().size());
            for (const auto& expr : stmt_.select_list()) {
                storage::Value val;
                auto eval_st = evaluator.evaluate(expr.get(), rec, &schema, val);
                if (!eval_st.ok()) {
                    out_result = ExecutionResult::Error(eval_st);
                    return eval_st;
                }
                row.push_back(std::move(val));
            }
            result_rows.push_back(std::move(row));
        }
    }

    // Apply LIMIT if specified
    if (stmt_.limit().has_value()) {
        const auto lim = *stmt_.limit();
        if (lim >= 0 && result_rows.size() > static_cast<std::size_t>(lim)) {
            result_rows.resize(static_cast<std::size_t>(lim));
        }
    }

    out_result = ExecutionResult::QueryResult(std::move(col_names), std::move(col_types), std::move(result_rows));
    return common::Status::Ok();
}

} // namespace querycraft::execution
