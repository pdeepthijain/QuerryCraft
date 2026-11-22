#pragma once

#include "common/status.hpp"
#include "storage/record.hpp"
#include "storage/schema.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace querycraft::execution {

class ExecutionResult {
public:
    ExecutionResult() = default;

    static ExecutionResult Error(common::Status status);
    static ExecutionResult CommandResult(std::string message);
    static ExecutionResult RowsAffected(std::size_t count, std::string message);
    static ExecutionResult QueryResult(std::vector<std::string> column_names,
                                      std::vector<storage::DataType> column_types,
                                      std::vector<std::vector<storage::Value>> rows);

    [[nodiscard]] const common::Status& status() const noexcept { return status_; }
    [[nodiscard]] bool is_query() const noexcept { return is_query_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] std::size_t rows_affected() const noexcept { return rows_affected_; }
    [[nodiscard]] const std::vector<std::string>& column_names() const noexcept { return column_names_; }
    [[nodiscard]] const std::vector<storage::DataType>& column_types() const noexcept { return column_types_; }
    [[nodiscard]] const std::vector<std::vector<storage::Value>>& rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }

    [[nodiscard]] std::string format_table() const;
    [[nodiscard]] std::string to_string() const;

private:
    common::Status status_{common::Status::Ok()};
    bool is_query_{false};
    std::string message_{""};
    std::size_t rows_affected_{0};
    std::vector<std::string> column_names_;
    std::vector<storage::DataType> column_types_;
    std::vector<std::vector<storage::Value>> rows_;
};

} // namespace querycraft::execution
