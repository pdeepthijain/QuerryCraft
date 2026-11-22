#include "execution/delete_executor.hpp"
#include "execution/expression_evaluator.hpp"
#include "index/index_key.hpp"

namespace querycraft::execution {

common::Status DeleteExecutor::execute(ExecutionContext& context, ExecutionResult& out_result) {
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

    // Scan all records from table
    std::vector<storage::Record> all_records;
    auto scan_st = context.database().scan_table(table_name, all_records);
    if (!scan_st.ok()) {
        out_result = ExecutionResult::Error(scan_st);
        return scan_st;
    }

    std::size_t num_deleted = 0;
    bool any_index_modified = false;

    // Filter and delete matching records
    for (const auto& rec : all_records) {
        bool matches = false;
        if (stmt_.where_clause() != nullptr) {
            auto pred_st = evaluator.evaluate_predicate(stmt_.where_clause(), rec, schema, matches);
            if (!pred_st.ok()) {
                out_result = ExecutionResult::Error(pred_st);
                return pred_st;
            }
        } else {
            // Unconditional DELETE deletes all rows
            matches = true;
        }

        if (!matches) {
            continue;
        }

        auto del_st = context.database().delete_record(table_name, rec.rid());
        if (!del_st.ok()) {
            out_result = ExecutionResult::Error(del_st);
            return del_st;
        }

        // Maintain indexes: remove this record's entry from all indexes on this table
        if (context.index_manager() != nullptr) {
            auto index_names = context.index_manager()->list_table_indexes(table_name);
            for (const auto& idx_name : index_names) {
                const auto* meta = context.index_manager()->get_metadata(idx_name);
                if (meta != nullptr) {
                    const auto c_idx = schema.get_column_index(meta->column_name);
                    if (c_idx.has_value()) {
                        const auto& val = rec.get_value(*c_idx);
                        auto del_idx_st = context.index_manager()->delete_entry(idx_name, index::IndexKey(val), rec.rid());
                        if (!del_idx_st.ok()) {
                            out_result = ExecutionResult::Error(del_idx_st);
                            return del_idx_st;
                        }
                        any_index_modified = true;
                    }
                }
            }
        }

        num_deleted++;
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
        num_deleted,
        std::to_string(num_deleted) + " row" + (num_deleted == 1 ? "" : "s") + " deleted.");
    return common::Status::Ok();
}

} // namespace querycraft::execution
