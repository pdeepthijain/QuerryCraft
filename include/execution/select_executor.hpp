#pragma once

#include "execution/executor.hpp"
#include "parser/ast.hpp"

namespace querycraft::execution {

class SelectExecutor : public Executor {
public:
    explicit SelectExecutor(const parser::SelectStatement& stmt) : stmt_(stmt) {}

    common::Status execute(ExecutionContext& context, ExecutionResult& out_result) override;

private:
    const parser::SelectStatement& stmt_;
};

} // namespace querycraft::execution
