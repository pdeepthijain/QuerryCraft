#include "execution/update_executor.hpp"
#include "execution/expression_evaluator.hpp"
#include "index/index_key.hpp"

#include <algorithm>
#include <cctype>

namespace querycraft::execution {

common::Status UpdateExecutor::execute(ExecutionContext& context, ExecutionResult& out_result) {
    const auto& table_name = stmt_.table_name();
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

    if (stmt_.assignments().empty()) {
        auto st = common::Status::InvalidArgument("UPDATE statement requires at least one SET assignment");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    // Validate that all assigned column names exist in schema
    std::vector<std::size_t> assignment_indices;
    assignment_indices.reserve(stmt_.assignments().size());

    for (const auto& assign : stmt_.assignments()) {
        const auto col_idx = schema.get_column_index(assign.column);
        if (!col_idx.has_value()) {
            auto st = common::Status::NotFound(
                "Column '" + assign.column + "' does not exist in table '" + table_name + "'");
            out_result = ExecutionResult::Error(st);
            return st;
        }
        assignment_indices.push_back(*col_idx);
    }

    // Scan all records from table
    std::vector<storage::Record> all_records;
    auto scan_st = context.database().scan_table(table_name, all_records);
    if (!scan_st.ok()) {
        out_result = ExecutionResult::Error(scan_st);
        return scan_st;
    }

    std::size_t num_updated = 0;
    bool any_index_modified = false;

    // Filter and update matching records
    for (const auto& old_rec : all_records) {
        bool matches = false;
        if (stmt_.where_clause() != nullptr) {
            auto pred_st = evaluator.evaluate_predicate(stmt_.where_clause(), old_rec, schema, matches);
            if (!pred_st.ok()) {
                out_result = ExecutionResult::Error(pred_st);
                return pred_st;
            }
        } else {
            matches = true;
        }

        if (!matches) {
            continue;
        }

        // Create updated record
        std::vector<storage::Value> new_values = old_rec.values();

        for (std::size_t i = 0; i < stmt_.assignments().size(); ++i) {
            const auto target_idx = assignment_indices[i];
            const auto& col = schema.get_column(target_idx);

            storage::Value val;
            auto eval_st = evaluator.evaluate(stmt_.assignments()[i].value.get(), &old_rec, &schema, val);
            if (!eval_st.ok()) {
                out_result = ExecutionResult::Error(eval_st);
                return eval_st;
            }

            if (val.is_null()) {
                if (!col.is_nullable() || col.is_primary_key()) {
                    auto st = common::Status::InvalidArgument("Column '" + col.name() + "' cannot be set to NULL");
                    out_result = ExecutionResult::Error(st);
                    return st;
                }
            } else {
                if (col.type() == storage::DataType::INT) {
                    if (!val.is_int()) {
                        auto st = common::Status::InvalidArgument("Type mismatch: expected INT for column '" + col.name() + "'");
                        out_result = ExecutionResult::Error(st);
                        return st;
                    }
                } else if (col.type() == storage::DataType::FLOAT) {
                    if (val.is_int()) {
                        val = storage::Value(static_cast<double>(val.as_int()));
                    } else if (!val.is_float()) {
                        auto st = common::Status::InvalidArgument("Type mismatch: expected FLOAT for column '" + col.name() + "'");
                        out_result = ExecutionResult::Error(st);
                        return st;
                    }
                } else if (col.type() == storage::DataType::TEXT || col.type() == storage::DataType::VARCHAR) {
                    if (!val.is_string()) {
                        auto st = common::Status::InvalidArgument("Type mismatch: expected string for column '" + col.name() + "'");
                        out_result = ExecutionResult::Error(st);
                        return st;
                    }
                    if (col.type() == storage::DataType::VARCHAR && col.length() > 0) {
                        if (val.as_string().size() > col.length()) {
                            auto st = common::Status::InvalidArgument(
                                "Value exceeds VARCHAR(" + std::to_string(col.length()) + ") capacity for column '" + col.name() + "'");
                            out_result = ExecutionResult::Error(st);
                            return st;
                        }
                    }
                }
            }

            new_values[target_idx] = std::move(val);
        }

        storage::Record new_rec(std::move(new_values), old_rec.rid());
        auto val_st = new_rec.validate(schema);
        if (!val_st.ok()) {
            out_result = ExecutionResult::Error(val_st);
            return val_st;
        }

        auto upd_st = context.database().update_record(table_name, old_rec.rid(), new_rec);
        if (!upd_st.ok()) {
            out_result = ExecutionResult::Error(upd_st);
            return upd_st;
        }

        // Maintain indexes
        if (context.index_manager() != nullptr) {
            auto index_names = context.index_manager()->list_table_indexes(table_name);
            for (const auto& idx_name : index_names) {
                const auto* meta = context.index_manager()->get_metadata(idx_name);
                if (meta != nullptr) {
                    const auto c_idx = schema.get_column_index(meta->column_name);
                    if (c_idx.has_value()) {
                        const auto& old_v = old_rec.get_value(*c_idx);
                        const auto& new_v = new_rec.get_value(*c_idx);
                        if (old_v != new_v) {
                            auto del_idx_st = context.index_manager()->delete_entry(idx_name, index::IndexKey(old_v), old_rec.rid());
                            if (!del_idx_st.ok()) {
                                out_result = ExecutionResult::Error(del_idx_st);
                                return del_idx_st;
                            }
                            auto ins_idx_st = context.index_manager()->insert_entry(idx_name, index::IndexKey(new_v), old_rec.rid());
                            if (!ins_idx_st.ok()) {
                                out_result = ExecutionResult::Error(ins_idx_st);
                                return ins_idx_st;
                            }
                            any_index_modified = true;
                        }
                    }
                }
            }
        }

        num_updated++;
    }

    if (any_index_modified && context.index_manager() != nullptr) {
        page_id_t cat_pid = context.index_manager()->catalog_page_id();
        auto persist_st = context.index_manager()->persist(cat_pid);
        if (!persist_st.ok()) {
            out_result = ExecutionResult::Error(persist_st);
            return persist_st;
        }
    }

    out_result = ExecutionResult::RowsAffected(
        num_updated,
        std::to_string(num_updated) + " row" + (num_updated == 1 ? "" : "s") + " updated.");
    return common::Status::Ok();
}

} // namespace querycraft::execution
