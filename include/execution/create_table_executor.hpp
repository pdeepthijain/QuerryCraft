#pragma once

#include "execution/executor.hpp"
#include "parser/ast.hpp"

namespace querycraft::execution {

class CreateTableExecutor : public Executor {
public:
    explicit CreateTableExecutor(const parser::CreateTableStatement& stmt) : stmt_(stmt) {}

    common::Status execute(ExecutionContext& context, ExecutionResult& out_result) override;

private:
    const parser::CreateTableStatement& stmt_;
};

} // namespace querycraft::execution
