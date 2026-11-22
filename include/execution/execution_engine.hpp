#pragma once

#include "execution/execution_context.hpp"
#include "execution/execution_result.hpp"
#include "parser/ast.hpp"

#include <memory>
#include <string_view>

namespace querycraft::execution {

class ExecutionEngine {
public:
    explicit ExecutionEngine(ExecutionContext context);
    ExecutionEngine(storage::Database& database,
                    index::IndexManager* index_manager = nullptr,
                    buffer::BufferPoolManager* buffer_pool_manager = nullptr);

    // Executes a pre-parsed AST statement
    ExecutionResult execute_statement(const parser::Statement& stmt);

    // Parses and executes a raw SQL string
    ExecutionResult execute(std::string_view sql);

    [[nodiscard]] ExecutionContext& context() noexcept { return context_; }
    [[nodiscard]] const ExecutionContext& context() const noexcept { return context_; }

private:
    ExecutionContext context_;
};

} // namespace querycraft::execution
