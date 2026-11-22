#pragma once

#include "execution/executor.hpp"
#include "parser/ast.hpp"

namespace querycraft::execution {

class UpdateExecutor : public Executor {
public:
    explicit UpdateExecutor(const parser::UpdateStatement& stmt) : stmt_(stmt) {}

    common::Status execute(ExecutionContext& context, ExecutionResult& out_result) override;

private:
    const parser::UpdateStatement& stmt_;
};

} // namespace querycraft::execution
