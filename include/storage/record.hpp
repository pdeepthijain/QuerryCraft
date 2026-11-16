#pragma once

#include "common/status.hpp"
#include "storage/page.hpp"
#include "storage/schema.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace querycraft::storage {

// Represents a dynamically-typed SQL value
class Value {
public:
    using VariantType = std::variant<std::monostate, std::int64_t, double, std::string>;

    Value() : data_(std::monostate{}) {} // Represents NULL
    Value(std::nullptr_t) : data_(std::monostate{}) {}
    Value(std::int64_t val) : data_(val) {}
    Value(int val) : data_(static_cast<std::int64_t>(val)) {}
    Value(double val) : data_(val) {}
    Value(std::string val) : data_(std::move(val)) {}
    Value(std::string_view val) : data_(std::string(val)) {}
    Value(const char* val) : data_(std::string(val)) {}

    [[nodiscard]] bool is_null() const noexcept {
        return std::holds_alternative<std::monostate>(data_);
    }
    [[nodiscard]] bool is_int() const noexcept {
        return std::holds_alternative<std::int64_t>(data_);
    }
    [[nodiscard]] bool is_float() const noexcept {
        return std::holds_alternative<double>(data_);
    }
    [[nodiscard]] bool is_string() const noexcept {
        return std::holds_alternative<std::string>(data_);
    }

    [[nodiscard]] std::int64_t as_int() const;
    [[nodiscard]] double as_float() const;
    [[nodiscard]] const std::string& as_string() const;

    [[nodiscard]] const VariantType& raw() const noexcept { return data_; }
    [[nodiscard]] std::string to_string() const;

    bool operator==(const Value& other) const = default;

private:
    VariantType data_;
};

// Represents a row of values in a table
class Record {
public:
    Record() = default;
    explicit Record(std::vector<Value> values, RecordId rid = RecordId{})
        : values_(std::move(values)), rid_(rid) {}

    [[nodiscard]] const std::vector<Value>& values() const noexcept { return values_; }
    [[nodiscard]] std::size_t value_count() const noexcept { return values_.size(); }
    [[nodiscard]] const Value& get_value(std::size_t index) const;
    void set_value(std::size_t index, Value value);

    [[nodiscard]] RecordId rid() const noexcept { return rid_; }
    void set_rid(RecordId rid) noexcept { rid_ = rid; }

    // Validation against schema
    [[nodiscard]] common::Status validate(const Schema& schema) const;

    // Binary serialization and deserialization
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    static common::Status deserialize(std::span<const std::uint8_t> bytes, Record& out_record);

    bool operator==(const Record& other) const = default;

private:
    std::vector<Value> values_;
    RecordId rid_{};
};

} // namespace querycraft::storage
