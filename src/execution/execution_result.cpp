#include "execution/execution_result.hpp"

#include <algorithm>
#include <sstream>

namespace querycraft::execution {

ExecutionResult ExecutionResult::Error(common::Status status) {
    ExecutionResult result;
    result.status_ = std::move(status);
    result.message_ = result.status_.message();
    return result;
}

ExecutionResult ExecutionResult::CommandResult(std::string message) {
    ExecutionResult result;
    result.status_ = common::Status::Ok();
    result.is_query_ = false;
    result.message_ = std::move(message);
    result.rows_affected_ = 0;
    return result;
}

ExecutionResult ExecutionResult::RowsAffected(std::size_t count, std::string message) {
    ExecutionResult result;
    result.status_ = common::Status::Ok();
    result.is_query_ = false;
    result.rows_affected_ = count;
    result.message_ = std::move(message);
    return result;
}

ExecutionResult ExecutionResult::QueryResult(std::vector<std::string> column_names,
                                            std::vector<storage::DataType> column_types,
                                            std::vector<std::vector<storage::Value>> rows) {
    ExecutionResult result;
    result.status_ = common::Status::Ok();
    result.is_query_ = true;
    result.column_names_ = std::move(column_names);
    result.column_types_ = std::move(column_types);
    result.rows_ = std::move(rows);
    result.rows_affected_ = result.rows_.size();
    return result;
}

std::string ExecutionResult::format_table() const {
    if (!status_.ok()) {
        return "Error: " + status_.message();
    }

    if (!is_query_) {
        return message_;
    }

    if (column_names_.empty()) {
        std::ostringstream ss;
        ss << rows_.size() << " row" << (rows_.size() == 1 ? "" : "s") << " returned.";
        return ss.str();
    }

    // Calculate maximum width for each column
    std::vector<std::size_t> col_widths(column_names_.size(), 0);
    for (std::size_t i = 0; i < column_names_.size(); ++i) {
        col_widths[i] = std::max(col_widths[i], column_names_[i].size());
    }

    for (const auto& row : rows_) {
        for (std::size_t i = 0; i < row.size() && i < col_widths.size(); ++i) {
            col_widths[i] = std::max(col_widths[i], row[i].to_string().size());
        }
    }

    // Minimum column width for aesthetic spacing
    for (auto& w : col_widths) {
        w = std::max(w, std::size_t{1});
    }

    std::ostringstream ss;

    // Helper to print separator: +----+-------+
    auto print_separator = [&]() {
        ss << '+';
        for (const auto w : col_widths) {
            ss << std::string(w + 2, '-') << '+';
        }
        ss << '\n';
    };

    print_separator();

    // Print Header
    ss << '|';
    for (std::size_t i = 0; i < column_names_.size(); ++i) {
        ss << ' ' << column_names_[i];
        const auto pad = col_widths[i] - column_names_[i].size();
        ss << std::string(pad, ' ') << " |";
    }
    ss << '\n';

    print_separator();

    // Print Rows
    for (const auto& row : rows_) {
        ss << '|';
        for (std::size_t i = 0; i < column_names_.size(); ++i) {
            const std::string val_str = (i < row.size()) ? row[i].to_string() : "NULL";
            ss << ' ' << val_str;
            const auto pad = (col_widths[i] >= val_str.size()) ? (col_widths[i] - val_str.size()) : 0;
            ss << std::string(pad, ' ') << " |";
        }
        ss << '\n';
    }

    print_separator();

    ss << rows_.size() << " row" << (rows_.size() == 1 ? "" : "s") << " returned.";
    return ss.str();
}

std::string ExecutionResult::to_string() const {
    if (!status_.ok()) {
        return "Error: " + status_.message();
    }
    if (is_query_) {
        return format_table();
    }
    return message_;
}

} // namespace querycraft::execution
