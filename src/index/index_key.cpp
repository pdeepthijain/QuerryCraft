#include "index/index_key.hpp"

#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace querycraft::index {

namespace {

inline void append_uint16(std::vector<std::uint8_t>& buf, std::uint16_t val) {
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

IndexKey::IndexKey(const storage::Value& val) {
    if (val.is_null()) {
        data_ = std::monostate{};
    } else if (val.is_int()) {
        data_ = val.as_int();
    } else if (val.is_float()) {
        data_ = val.as_float();
    } else if (val.is_string()) {
        data_ = val.as_string();
    }
}

KeyType IndexKey::type() const noexcept {
    if (is_null()) return KeyType::NULL_KEY;
    if (is_int()) return KeyType::INT;
    if (is_float()) return KeyType::FLOAT;
    return KeyType::TEXT;
}

std::int64_t IndexKey::as_int() const {
    if (!is_int()) {
        throw std::bad_variant_access();
    }
    return std::get<std::int64_t>(data_);
}

double IndexKey::as_float() const {
    if (!is_float()) {
        throw std::bad_variant_access();
    }
    return std::get<double>(data_);
}

const std::string& IndexKey::as_text() const {
    if (!is_text()) {
        throw std::bad_variant_access();
    }
    return std::get<std::string>(data_);
}

storage::Value IndexKey::to_value() const {
    if (is_null()) return storage::Value(nullptr);
    if (is_int()) return storage::Value(as_int());
    if (is_float()) return storage::Value(as_float());
    return storage::Value(as_text());
}

std::string IndexKey::to_string() const {
    if (is_null()) return "NULL";
    if (is_int()) return std::to_string(as_int());
    if (is_float()) {
        std::ostringstream oss;
        oss << as_float();
        return oss.str();
    }
    return as_text();
}

int IndexKey::compare(const IndexKey& other) const {
    if (is_null() && other.is_null()) {
        return 0;
    }
    if (is_null()) {
        return -1; // NULLs first
    }
    if (other.is_null()) {
        return 1;
    }

    if (type() != other.type()) {
        throw std::invalid_argument(
            "Cannot compare incompatible IndexKey types: " +
            std::string(key_type_to_string(type())) + " vs " +
            std::string(key_type_to_string(other.type())));
    }

    switch (type()) {
        case KeyType::INT: {
            const auto a = as_int();
            const auto b = other.as_int();
            if (a < b) return -1;
            if (a > b) return 1;
            return 0;
        }
        case KeyType::FLOAT: {
            const auto a = as_float();
            const auto b = other.as_float();
            if (a < b) return -1;
            if (a > b) return 1;
            return 0;
        }
        case KeyType::TEXT: {
            const auto& a = as_text();
            const auto& b = other.as_text();
            if (a < b) return -1;
            if (a > b) return 1;
            return 0;
        }
        default:
            return 0;
    }
}

void IndexKey::serialize(std::vector<std::uint8_t>& buf) const {
    buf.push_back(static_cast<std::uint8_t>(type()));
    switch (type()) {
        case KeyType::NULL_KEY:
            break;
        case KeyType::INT:
            append_int64(buf, as_int());
            break;
        case KeyType::FLOAT:
            append_double(buf, as_float());
            break;
        case KeyType::TEXT: {
            const auto& text = as_text();
            append_uint16(buf, static_cast<std::uint16_t>(text.size()));
            buf.insert(buf.end(), text.begin(), text.end());
            break;
        }
    }
}

std::vector<std::uint8_t> IndexKey::serialize() const {
    std::vector<std::uint8_t> buf;
    serialize(buf);
    return buf;
}

common::Status IndexKey::deserialize(std::span<const std::uint8_t> bytes,
                                    std::size_t& offset,
                                    IndexKey& out_key) {
    if (offset >= bytes.size()) {
        return common::Status::InvalidArgument("IndexKey deserialize: unexpected end of bytes");
    }

    const auto type_val = bytes[offset++];
    if (type_val > static_cast<std::uint8_t>(KeyType::TEXT)) {
        return common::Status::InvalidArgument("IndexKey deserialize: invalid type tag " + std::to_string(type_val));
    }

    const auto ktype = static_cast<KeyType>(type_val);
    switch (ktype) {
        case KeyType::NULL_KEY:
            out_key = IndexKey();
            return common::Status::Ok();
        case KeyType::INT: {
            if (offset + sizeof(std::int64_t) > bytes.size()) {
                return common::Status::InvalidArgument("IndexKey deserialize: unexpected EOF reading INT");
            }
            std::int64_t val = 0;
            std::memcpy(&val, bytes.data() + offset, sizeof(val));
            offset += sizeof(val);
            out_key = IndexKey(val);
            return common::Status::Ok();
        }
        case KeyType::FLOAT: {
            if (offset + sizeof(double) > bytes.size()) {
                return common::Status::InvalidArgument("IndexKey deserialize: unexpected EOF reading FLOAT");
            }
            double val = 0.0;
            std::memcpy(&val, bytes.data() + offset, sizeof(val));
            offset += sizeof(val);
            out_key = IndexKey(val);
            return common::Status::Ok();
        }
        case KeyType::TEXT: {
            if (offset + sizeof(std::uint16_t) > bytes.size()) {
                return common::Status::InvalidArgument("IndexKey deserialize: unexpected EOF reading TEXT length");
            }
            std::uint16_t len = 0;
            std::memcpy(&len, bytes.data() + offset, sizeof(len));
            offset += sizeof(len);

            if (offset + len > bytes.size()) {
                return common::Status::InvalidArgument("IndexKey deserialize: unexpected EOF reading TEXT characters");
            }
            std::string text(reinterpret_cast<const char*>(bytes.data() + offset), len);
            offset += len;
            out_key = IndexKey(std::move(text));
            return common::Status::Ok();
        }
    }

    return common::Status::InvalidArgument("IndexKey deserialize: unknown error");
}

} // namespace querycraft::index
