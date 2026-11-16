#pragma once

#include "common/status.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace querycraft::storage {

// Storage-level Data Types (kept decoupled from the SQL parser)
enum class DataType : std::uint8_t {
    INT = 0,
    FLOAT = 1,
    TEXT = 2,
    VARCHAR = 3
};

[[nodiscard]] constexpr std::string_view data_type_to_string(DataType type) noexcept {
    switch (type) {
        case DataType::INT:     return "INT";
        case DataType::FLOAT:   return "FLOAT";
        case DataType::TEXT:    return "TEXT";
        case DataType::VARCHAR: return "VARCHAR";
    }
    return "UNKNOWN";
}

class Column {
public:
    Column() = default;
    Column(std::string name,
           DataType type,
           bool is_primary_key = false,
           bool is_nullable = true,
           std::uint32_t length = 0)
        : name_(std::move(name)),
          type_(type),
          is_primary_key_(is_primary_key),
          is_nullable_(is_nullable),
          length_(length) {}

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] DataType type() const noexcept { return type_; }
    [[nodiscard]] bool is_primary_key() const noexcept { return is_primary_key_; }
    [[nodiscard]] bool is_nullable() const noexcept { return is_nullable_; }
    [[nodiscard]] std::uint32_t length() const noexcept { return length_; }

    bool operator==(const Column& other) const = default;

private:
    std::string name_;
    DataType type_{DataType::INT};
    bool is_primary_key_{false};
    bool is_nullable_{true};
    std::uint32_t length_{0};
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

    [[nodiscard]] const std::vector<Column>& columns() const noexcept { return columns_; }
    [[nodiscard]] std::size_t column_count() const noexcept { return columns_.size(); }

    [[nodiscard]] const Column& get_column(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> get_column_index(std::string_view name) const;
    [[nodiscard]] bool has_column(std::string_view name) const;

    [[nodiscard]] std::vector<std::string> primary_key_columns() const;
    [[nodiscard]] std::optional<std::size_t> primary_key_index() const;

    // Binary serialization and deserialization for catalog persistence
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    static common::Status deserialize(std::span<const std::uint8_t> bytes, Schema& out_schema);

    bool operator==(const Schema& other) const = default;

private:
    std::vector<Column> columns_;
};

} // namespace querycraft::storage
