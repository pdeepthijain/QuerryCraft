#include "storage/record.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace querycraft::storage {

namespace {

constexpr std::uint8_t TAG_NULL = 0;
constexpr std::uint8_t TAG_INT = 1;
constexpr std::uint8_t TAG_FLOAT = 2;
constexpr std::uint8_t TAG_STRING = 3;

inline void append_uint16(std::vector<std::uint8_t>& buf, std::uint16_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(val));
}

inline void append_uint32(std::vector<std::uint8_t>& buf, std::uint32_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(val));
}

inline void append_int64(std::vector<std::uint8_t>& buf, std::int64_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(val));
}

inline void append_double(std::vector<std::uint8_t>& buf, double val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(val));
}

} // namespace

std::int64_t Value::as_int() const {
    if (!is_int()) {
        throw std::bad_variant_access();
    }
    return std::get<std::int64_t>(data_);
}

double Value::as_float() const {
    if (!is_float()) {
        throw std::bad_variant_access();
    }
    return std::get<double>(data_);
}

const std::string& Value::as_string() const {
    if (!is_string()) {
        throw std::bad_variant_access();
    }
    return std::get<std::string>(data_);
}

std::string Value::to_string() const {
    if (is_null()) {
        return "NULL";
    }
    if (is_int()) {
        return std::to_string(as_int());
    }
    if (is_float()) {
        std::ostringstream oss;
        oss << as_float();
        return oss.str();
    }
    return as_string();
}

const Value& Record::get_value(std::size_t index) const {
    if (index >= values_.size()) {
        throw std::out_of_range("Record value index out of range: " + std::to_string(index));
    }
    return values_[index];
}

void Record::set_value(std::size_t index, Value value) {
    if (index >= values_.size()) {
        throw std::out_of_range("Record value index out of range: " + std::to_string(index));
    }
    values_[index] = std::move(value);
}

common::Status Record::validate(const Schema& schema) const {
    if (values_.size() != schema.column_count()) {
        return common::Status::InvalidArgument(
            "Record value count (" + std::to_string(values_.size()) +
            ") does not match schema column count (" + std::to_string(schema.column_count()) + ")");
    }

    for (std::size_t i = 0; i < values_.size(); ++i) {
        const auto& col = schema.get_column(i);
        const auto& val = values_[i];

        if (val.is_null()) {
            if (!col.is_nullable() || col.is_primary_key()) {
                return common::Status::InvalidArgument(
                    "Column '" + col.name() + "' cannot be NULL");
            }
            continue;
        }

        switch (col.type()) {
            case DataType::INT:
                if (!val.is_int()) {
                    return common::Status::InvalidArgument(
                        "Type mismatch for column '" + col.name() + "': expected INT");
                }
                break;
            case DataType::FLOAT:
                if (!val.is_float()) {
                    return common::Status::InvalidArgument(
                        "Type mismatch for column '" + col.name() + "': expected FLOAT");
                }
                break;
            case DataType::TEXT:
                if (!val.is_string()) {
                    return common::Status::InvalidArgument(
                        "Type mismatch for column '" + col.name() + "': expected TEXT");
                }
                break;
            case DataType::VARCHAR:
                if (!val.is_string()) {
                    return common::Status::InvalidArgument(
                        "Type mismatch for column '" + col.name() + "': expected VARCHAR");
                }
                if (col.length() > 0 && val.as_string().size() > col.length()) {
                    return common::Status::InvalidArgument(
                        "Value length (" + std::to_string(val.as_string().size()) +
                        ") exceeds VARCHAR(" + std::to_string(col.length()) + ") constraint for column '" +
                        col.name() + "'");
                }
                break;
        }
    }

    return common::Status::Ok();
}

std::vector<std::uint8_t> Record::serialize() const {
    std::vector<std::uint8_t> bytes;
    append_uint16(bytes, static_cast<std::uint16_t>(values_.size()));

    for (const auto& val : values_) {
        if (val.is_null()) {
            bytes.push_back(TAG_NULL);
        } else if (val.is_int()) {
            bytes.push_back(TAG_INT);
            append_int64(bytes, val.as_int());
        } else if (val.is_float()) {
            bytes.push_back(TAG_FLOAT);
            append_double(bytes, val.as_float());
        } else if (val.is_string()) {
            bytes.push_back(TAG_STRING);
            const auto& str = val.as_string();
            append_uint32(bytes, static_cast<std::uint32_t>(str.size()));
            bytes.insert(bytes.end(), str.begin(), str.end());
        }
    }

    return bytes;
}

common::Status Record::deserialize(std::span<const std::uint8_t> bytes, Record& out_record) {
    if (bytes.size() < sizeof(std::uint16_t)) {
        return common::Status::InvalidArgument("Corrupted record bytes: insufficient size for header");
    }

    std::size_t offset = 0;
    std::uint16_t num_values = 0;
    std::memcpy(&num_values, bytes.data() + offset, sizeof(num_values));
    offset += sizeof(num_values);

    std::vector<Value> vals;
    vals.reserve(num_values);

    for (std::uint16_t i = 0; i < num_values; ++i) {
        if (offset >= bytes.size()) {
            return common::Status::InvalidArgument("Corrupted record bytes: unexpected EOF while reading value tag");
        }

        const auto tag = bytes[offset++];
        switch (tag) {
            case TAG_NULL: {
                vals.emplace_back();
                break;
            }
            case TAG_INT: {
                if (offset + sizeof(std::int64_t) > bytes.size()) {
                    return common::Status::InvalidArgument("Corrupted record bytes: unexpected EOF reading INT");
                }
                std::int64_t int_val = 0;
                std::memcpy(&int_val, bytes.data() + offset, sizeof(int_val));
                offset += sizeof(int_val);
                vals.emplace_back(int_val);
                break;
            }
            case TAG_FLOAT: {
                if (offset + sizeof(double) > bytes.size()) {
                    return common::Status::InvalidArgument("Corrupted record bytes: unexpected EOF reading FLOAT");
                }
                double float_val = 0.0;
                std::memcpy(&float_val, bytes.data() + offset, sizeof(float_val));
                offset += sizeof(float_val);
                vals.emplace_back(float_val);
                break;
            }
            case TAG_STRING: {
                if (offset + sizeof(std::uint32_t) > bytes.size()) {
                    return common::Status::InvalidArgument("Corrupted record bytes: unexpected EOF reading string length");
                }
                std::uint32_t str_len = 0;
                std::memcpy(&str_len, bytes.data() + offset, sizeof(str_len));
                offset += sizeof(str_len);

                if (offset + str_len > bytes.size()) {
                    return common::Status::InvalidArgument("Corrupted record bytes: unexpected EOF reading string characters");
                }
                std::string str(reinterpret_cast<const char*>(bytes.data() + offset), str_len);
                offset += str_len;
                vals.emplace_back(std::move(str));
                break;
            }
            default:
                return common::Status::InvalidArgument("Corrupted record bytes: invalid type tag " + std::to_string(tag));
        }
    }

    out_record = Record(std::move(vals));
    return common::Status::Ok();
}

} // namespace querycraft::storage
