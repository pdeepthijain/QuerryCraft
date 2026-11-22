#pragma once

#include "common/status.hpp"
#include "execution/execution_context.hpp"
#include "execution/execution_result.hpp"

namespace querycraft::execution {

class Executor {
public:
    virtual ~Executor() = default;

    virtual common::Status execute(ExecutionContext& context, ExecutionResult& out_result) = 0;
};

} // namespace querycraft::execution
