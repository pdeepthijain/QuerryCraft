#include "execution/create_index_executor.hpp"
#include "index/index_key.hpp"

namespace querycraft::execution {

common::Status CreateIndexExecutor::execute(ExecutionContext& context, ExecutionResult& out_result) {
    const auto& index_name = stmt_.index_name();
    const auto& table_name = stmt_.table_name();

    if (context.index_manager() == nullptr) {
        auto st = common::Status::NotSupported("IndexManager is not available in execution context");
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

    if (stmt_.columns().empty()) {
        auto st = common::Status::InvalidArgument("CREATE INDEX requires at least one indexed column");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    if (stmt_.columns().size() > 1) {
        auto st = common::Status::NotSupported("QueryCraft currently supports single-column B+ Tree indexes");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    const auto& col_name = stmt_.columns()[0];
    const auto& schema = table->schema();
    const auto col_idx = schema.get_column_index(col_name);
    if (!col_idx.has_value()) {
        auto st = common::Status::NotFound(
            "Column '" + col_name + "' does not exist in table '" + table_name + "'");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    const auto& col = schema.get_column(*col_idx);
    index::KeyType key_type = index::KeyType::INT;

    switch (col.type()) {
        case storage::DataType::INT:
            key_type = index::KeyType::INT;
            break;
        case storage::DataType::FLOAT:
            key_type = index::KeyType::FLOAT;
            break;
        case storage::DataType::TEXT:
        case storage::DataType::VARCHAR:
            key_type = index::KeyType::TEXT;
            break;
    }

    // Create index in index manager (handles duplicate checks)
    auto create_st = context.index_manager()->create_index(index_name, table_name, col_name, key_type);
    if (!create_st.ok()) {
        out_result = ExecutionResult::Error(create_st);
        return create_st;
    }

    // Populate index with all existing records from the table
    std::vector<storage::Record> existing_records;
    auto scan_st = context.database().scan_table(table_name, existing_records);
    if (!scan_st.ok()) {
        out_result = ExecutionResult::Error(scan_st);
        return scan_st;
    }

    for (const auto& rec : existing_records) {
        const auto& val = rec.get_value(*col_idx);
        index::IndexKey key(val);
        auto insert_st = context.index_manager()->insert_entry(index_name, key, rec.rid());
        if (!insert_st.ok()) {
            out_result = ExecutionResult::Error(insert_st);
            return insert_st;
        }
    }

    // Persist index catalog and B+ Tree
    page_id_t cat_pid = context.index_manager()->catalog_page_id();
    auto persist_st = context.index_manager()->persist(cat_pid);
    if (!persist_st.ok()) {
        out_result = ExecutionResult::Error(persist_st);
        return persist_st;
    }

    out_result = ExecutionResult::CommandResult("Index created successfully.");
    return common::Status::Ok();
}

} // namespace querycraft::execution
