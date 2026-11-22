#include "execution/insert_executor.hpp"
#include "execution/expression_evaluator.hpp"
#include "index/index_key.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace querycraft::execution {

namespace {

std::string to_lower_ascii(std::string_view sv) {
    std::string result;
    result.reserve(sv.size());
    for (const char c : sv) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

} // namespace

common::Status InsertExecutor::execute(ExecutionContext& context, ExecutionResult& out_result) {
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

    std::vector<storage::Value> row_values(schema.column_count());
    std::vector<bool> assigned(schema.column_count(), false);

    if (stmt_.columns().empty()) {
        // Full column insertion: values count must exactly match schema column count
        if (stmt_.values().size() != schema.column_count()) {
            auto st = common::Status::InvalidArgument(
                "INSERT value count mismatch: expected " + std::to_string(schema.column_count()) +
                ", got " + std::to_string(stmt_.values().size()));
            out_result = ExecutionResult::Error(st);
            return st;
        }

        for (std::size_t i = 0; i < schema.column_count(); ++i) {
            storage::Value val;
            auto eval_st = evaluator.evaluate_constant(stmt_.values()[i].get(), val);
            if (!eval_st.ok()) {
                out_result = ExecutionResult::Error(eval_st);
                return eval_st;
            }

            const auto& col = schema.get_column(i);

            // Nullability check
            if (val.is_null()) {
                if (!col.is_nullable() || col.is_primary_key()) {
                    auto st = common::Status::InvalidArgument("Column '" + col.name() + "' cannot be NULL");
                    out_result = ExecutionResult::Error(st);
                    return st;
                }
            } else {
                // Type verification and coercion
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

            row_values[i] = std::move(val);
            assigned[i] = true;
        }
    } else {
        // Explicit column list specified
        if (stmt_.columns().size() != stmt_.values().size()) {
            auto st = common::Status::InvalidArgument(
                "INSERT column count (" + std::to_string(stmt_.columns().size()) +
                ") does not match value count (" + std::to_string(stmt_.values().size()) + ")");
            out_result = ExecutionResult::Error(st);
            return st;
        }

        std::unordered_set<std::string> seen_cols;
        for (std::size_t i = 0; i < stmt_.columns().size(); ++i) {
            const auto& col_name = stmt_.columns()[i];
            const auto lower_col = to_lower_ascii(col_name);
            if (seen_cols.find(lower_col) != seen_cols.end()) {
                auto st = common::Status::InvalidArgument("Duplicate column in INSERT list: '" + col_name + "'");
                out_result = ExecutionResult::Error(st);
                return st;
            }
            seen_cols.insert(lower_col);

            const auto col_idx = schema.get_column_index(col_name);
            if (!col_idx.has_value()) {
                auto st = common::Status::NotFound("Column '" + col_name + "' does not exist in table '" + table_name + "'");
                out_result = ExecutionResult::Error(st);
                return st;
            }

            storage::Value val;
            auto eval_st = evaluator.evaluate_constant(stmt_.values()[i].get(), val);
            if (!eval_st.ok()) {
                out_result = ExecutionResult::Error(eval_st);
                return eval_st;
            }

            const auto& col = schema.get_column(*col_idx);
            if (val.is_null()) {
                if (!col.is_nullable() || col.is_primary_key()) {
                    auto st = common::Status::InvalidArgument("Column '" + col.name() + "' cannot be NULL");
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

            row_values[*col_idx] = std::move(val);
            assigned[*col_idx] = true;
        }

        // Fill unassigned columns with NULL or check required NOT NULL
        for (std::size_t i = 0; i < schema.column_count(); ++i) {
            if (!assigned[i]) {
                const auto& col = schema.get_column(i);
                if (!col.is_nullable() || col.is_primary_key()) {
                    auto st = common::Status::InvalidArgument(
                        "Field '" + col.name() + "' does not have a default value and cannot be NULL");
                    out_result = ExecutionResult::Error(st);
                    return st;
                }
                row_values[i] = storage::Value{}; // NULL
            }
        }
    }

    storage::Record record(std::move(row_values));
    const auto val_st = record.validate(schema);
    if (!val_st.ok()) {
        out_result = ExecutionResult::Error(val_st);
        return val_st;
    }

    storage::RecordId rid;
    auto insert_st = context.database().insert_record(table_name, record, rid);
    if (!insert_st.ok()) {
        out_result = ExecutionResult::Error(insert_st);
        return insert_st;
    }

    // Maintain indexes
    if (context.index_manager() != nullptr) {
        auto index_names = context.index_manager()->list_table_indexes(table_name);
        for (const auto& idx_name : index_names) {
            const auto* meta = context.index_manager()->get_metadata(idx_name);
            if (meta != nullptr) {
                const auto c_idx = schema.get_column_index(meta->column_name);
                if (c_idx.has_value()) {
                    const auto& val = record.get_value(*c_idx);
                    index::IndexKey key(val);
                    auto idx_st = context.index_manager()->insert_entry(idx_name, key, rid);
                    if (!idx_st.ok()) {
                        out_result = ExecutionResult::Error(idx_st);
                        return idx_st;
                    }
                }
            }
        }

        page_id_t cat_pid = context.index_manager()->catalog_page_id();
        auto persist_st = context.index_manager()->persist(cat_pid);
        if (!persist_st.ok()) {
            out_result = ExecutionResult::Error(persist_st);
            return persist_st;
        }
    }

    out_result = ExecutionResult::RowsAffected(1, "1 row inserted.");
    return common::Status::Ok();
}

} // namespace querycraft::execution
