#include "execution/create_table_executor.hpp"

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

common::Status CreateTableExecutor::execute(ExecutionContext& context, ExecutionResult& out_result) {
    const auto& table_name = stmt_.table_name();
    if (table_name.empty()) {
        auto st = common::Status::InvalidArgument("Table name cannot be empty");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    if (!context.database().is_open()) {
        auto st = common::Status::IoError("Database is not open");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    if (context.database().has_table(table_name)) {
        auto st = common::Status::AlreadyExists("Table '" + table_name + "' already exists");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    if (stmt_.columns().empty()) {
        auto st = common::Status::InvalidArgument("Table must define at least one column");
        out_result = ExecutionResult::Error(st);
        return st;
    }

    std::vector<storage::Column> storage_columns;
    std::unordered_set<std::string> seen_columns;

    for (const auto& col_def : stmt_.columns()) {
        if (col_def.name.empty()) {
            auto st = common::Status::InvalidArgument("Column name cannot be empty");
            out_result = ExecutionResult::Error(st);
            return st;
        }

        const auto lower_col = to_lower_ascii(col_def.name);
        if (seen_columns.find(lower_col) != seen_columns.end()) {
            auto st = common::Status::InvalidArgument("Duplicate column name: '" + col_def.name + "'");
            out_result = ExecutionResult::Error(st);
            return st;
        }
        seen_columns.insert(lower_col);

        storage::DataType st_type = storage::DataType::INT;
        std::uint32_t col_len = 0;

        switch (col_def.data_type) {
            case parser::DataType::INT:
                st_type = storage::DataType::INT;
                break;
            case parser::DataType::FLOAT:
                st_type = storage::DataType::FLOAT;
                break;
            case parser::DataType::TEXT:
                st_type = storage::DataType::TEXT;
                break;
            case parser::DataType::VARCHAR:
                st_type = storage::DataType::VARCHAR;
                col_len = static_cast<std::uint32_t>(col_def.length.value_or(255));
                break;
        }

        // Check if marked as primary key in column def or in table constraint
        bool is_pk = col_def.is_primary_key;
        for (const auto& pk_name : stmt_.primary_key_columns()) {
            if (to_lower_ascii(pk_name) == lower_col) {
                is_pk = true;
                break;
            }
        }

        const bool is_nullable = !is_pk;
        storage_columns.emplace_back(col_def.name, st_type, is_pk, is_nullable, col_len);
    }

    storage::Schema schema(std::move(storage_columns));
    auto create_st = context.database().create_table(table_name, schema);
    if (!create_st.ok()) {
        out_result = ExecutionResult::Error(create_st);
        return create_st;
    }

    out_result = ExecutionResult::CommandResult("Table created successfully.");
    return common::Status::Ok();
}

} // namespace querycraft::execution
