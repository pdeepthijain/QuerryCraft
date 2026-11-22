#pragma once

#include "execution/executor.hpp"
#include "parser/ast.hpp"

namespace querycraft::execution {

class DeleteExecutor : public Executor {
public:
    explicit DeleteExecutor(const parser::DeleteStatement& stmt) : stmt_(stmt) {}

    common::Status execute(ExecutionContext& context, ExecutionResult& out_result) override;

private:
    const parser::DeleteStatement& stmt_;
};

} // namespace querycraft::execution
