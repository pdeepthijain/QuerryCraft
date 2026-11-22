#include "execution/execution_engine.hpp"
#include "execution/create_index_executor.hpp"
#include "execution/create_table_executor.hpp"
#include "execution/delete_executor.hpp"
#include "execution/insert_executor.hpp"
#include "execution/select_executor.hpp"
#include "execution/update_executor.hpp"
#include "parser/parser.hpp"

namespace querycraft::execution {

ExecutionEngine::ExecutionEngine(ExecutionContext context) : context_(std::move(context)) {}

ExecutionEngine::ExecutionEngine(storage::Database& database,
                                 index::IndexManager* index_manager,
                                 buffer::BufferPoolManager* buffer_pool_manager)
    : context_(database, index_manager, buffer_pool_manager) {}

ExecutionResult ExecutionEngine::execute_statement(const parser::Statement& stmt) {
    ExecutionResult result;

    switch (stmt.stmt_type()) {
        case parser::StatementType::CREATE_TABLE: {
            const auto& create_stmt = dynamic_cast<const parser::CreateTableStatement&>(stmt);
            CreateTableExecutor executor(create_stmt);
            (void)executor.execute(context_, result);
            break;
        }
        case parser::StatementType::INSERT: {
            const auto& insert_stmt = dynamic_cast<const parser::InsertStatement&>(stmt);
            InsertExecutor executor(insert_stmt);
            (void)executor.execute(context_, result);
            break;
        }
        case parser::StatementType::SELECT: {
            const auto& select_stmt = dynamic_cast<const parser::SelectStatement&>(stmt);
            SelectExecutor executor(select_stmt);
            (void)executor.execute(context_, result);
            break;
        }
        case parser::StatementType::UPDATE: {
            const auto& update_stmt = dynamic_cast<const parser::UpdateStatement&>(stmt);
            UpdateExecutor executor(update_stmt);
            (void)executor.execute(context_, result);
            break;
        }
        case parser::StatementType::DELETE: {
            const auto& delete_stmt = dynamic_cast<const parser::DeleteStatement&>(stmt);
            DeleteExecutor executor(delete_stmt);
            (void)executor.execute(context_, result);
            break;
        }
        case parser::StatementType::CREATE_INDEX: {
            const auto& index_stmt = dynamic_cast<const parser::CreateIndexStatement&>(stmt);
            CreateIndexExecutor executor(index_stmt);
            (void)executor.execute(context_, result);
            break;
        }
    }

    return result;
}

ExecutionResult ExecutionEngine::execute(std::string_view sql) {
    auto [stmt, status] = parser::Parser::parse(sql);
    if (!status.ok()) {
        return ExecutionResult::Error(status);
    }

    if (!stmt) {
        return ExecutionResult::Error(common::Status::InvalidArgument("Empty SQL statement"));
    }

    return execute_statement(*stmt);
}

} // namespace querycraft::execution
