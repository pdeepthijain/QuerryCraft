#pragma once

#include "execution/executor.hpp"
#include "parser/ast.hpp"

namespace querycraft::execution {

class CreateIndexExecutor : public Executor {
public:
    explicit CreateIndexExecutor(const parser::CreateIndexStatement& stmt) : stmt_(stmt) {}

    common::Status execute(ExecutionContext& context, ExecutionResult& out_result) override;

private:
    const parser::CreateIndexStatement& stmt_;
};

} // namespace querycraft::execution
