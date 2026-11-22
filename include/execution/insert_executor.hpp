#pragma once

#include "execution/executor.hpp"
#include "parser/ast.hpp"

namespace querycraft::execution {

class InsertExecutor : public Executor {
public:
    explicit InsertExecutor(const parser::InsertStatement& stmt) : stmt_(stmt) {}

    common::Status execute(ExecutionContext& context, ExecutionResult& out_result) override;

private:
    const parser::InsertStatement& stmt_;
};

} // namespace querycraft::execution
