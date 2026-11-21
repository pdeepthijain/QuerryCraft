#pragma once

#include "common/status.hpp"
#include "storage/record.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace querycraft::index {

enum class KeyType : std::uint8_t {
    NULL_KEY = 0,
    INT = 1,
    FLOAT = 2,
    TEXT = 3
};

[[nodiscard]] constexpr std::string_view key_type_to_string(KeyType type) noexcept {
    switch (type) {
        case KeyType::NULL_KEY: return "NULL";
        case KeyType::INT:      return "INT";
        case KeyType::FLOAT:    return "FLOAT";
        case KeyType::TEXT:     return "TEXT";
    }
    return "UNKNOWN";
}

class IndexKey {
public:
    using VariantType = std::variant<std::monostate, std::int64_t, double, std::string>;

    IndexKey() : data_(std::monostate{}) {}
    IndexKey(std::nullptr_t) : data_(std::monostate{}) {}
    IndexKey(std::int64_t val) : data_(val) {}
    IndexKey(int val) : data_(static_cast<std::int64_t>(val)) {}
    IndexKey(double val) : data_(val) {}
    IndexKey(std::string val) : data_(std::move(val)) {}
    IndexKey(std::string_view val) : data_(std::string(val)) {}
    IndexKey(const char* val) : data_(std::string(val)) {}
    explicit IndexKey(const storage::Value& val);

    [[nodiscard]] KeyType type() const noexcept;
    [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::monostate>(data_); }
    [[nodiscard]] bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(data_); }
    [[nodiscard]] bool is_float() const noexcept { return std::holds_alternative<double>(data_); }
    [[nodiscard]] bool is_text() const noexcept { return std::holds_alternative<std::string>(data_); }

    [[nodiscard]] std::int64_t as_int() const;
    [[nodiscard]] double as_float() const;
    [[nodiscard]] const std::string& as_text() const;

    [[nodiscard]] const VariantType& raw() const noexcept { return data_; }
    [[nodiscard]] storage::Value to_value() const;
    [[nodiscard]] std::string to_string() const;

    // Type-safe comparison. Throws std::invalid_argument if comparing incompatible types.
    [[nodiscard]] int compare(const IndexKey& other) const;

    bool operator==(const IndexKey& other) const { return compare(other) == 0; }
    bool operator!=(const IndexKey& other) const { return compare(other) != 0; }
    bool operator<(const IndexKey& other) const { return compare(other) < 0; }
    bool operator<=(const IndexKey& other) const { return compare(other) <= 0; }
    bool operator>(const IndexKey& other) const { return compare(other) > 0; }
    bool operator>=(const IndexKey& other) const { return compare(other) >= 0; }

    // Serialization
    void serialize(std::vector<std::uint8_t>& buf) const;
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    static common::Status deserialize(std::span<const std::uint8_t> bytes,
                                      std::size_t& offset,
                                      IndexKey& out_key);

private:
    VariantType data_;
};

} // namespace querycraft::index
