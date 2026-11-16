#include "storage/schema.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace querycraft::storage {

namespace {

std::string to_lower_ascii(std::string_view sv) {
    std::string result;
    result.reserve(sv.size());
    for (const char c : sv) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

inline void append_uint16(std::vector<std::uint8_t>& buf, std::uint16_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(val));
}

inline void append_uint32(std::vector<std::uint8_t>& buf, std::uint32_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(val));
}

} // namespace

const Column& Schema::get_column(std::size_t index) const {
    if (index >= columns_.size()) {
        throw std::out_of_range("Schema column index out of range: " + std::to_string(index));
    }
    return columns_[index];
}

std::optional<std::size_t> Schema::get_column_index(std::string_view name) const {
    const std::string target = to_lower_ascii(name);
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (to_lower_ascii(columns_[i].name()) == target) {
            return i;
        }
    }
    return std::nullopt;
}

bool Schema::has_column(std::string_view name) const {
    return get_column_index(name).has_value();
}

std::vector<std::string> Schema::primary_key_columns() const {
    std::vector<std::string> pks;
    for (const auto& col : columns_) {
        if (col.is_primary_key()) {
            pks.push_back(col.name());
        }
    }
    return pks;
}

std::optional<std::size_t> Schema::primary_key_index() const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].is_primary_key()) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<std::uint8_t> Schema::serialize() const {
    std::vector<std::uint8_t> bytes;
    append_uint16(bytes, static_cast<std::uint16_t>(columns_.size()));

    for (const auto& col : columns_) {
        const std::string& name = col.name();
        append_uint16(bytes, static_cast<std::uint16_t>(name.size()));
        bytes.insert(bytes.end(), name.begin(), name.end());
        bytes.push_back(static_cast<std::uint8_t>(col.type()));
        bytes.push_back(col.is_primary_key() ? 1 : 0);
        bytes.push_back(col.is_nullable() ? 1 : 0);
        append_uint32(bytes, col.length());
    }

    return bytes;
}

common::Status Schema::deserialize(std::span<const std::uint8_t> bytes, Schema& out_schema) {
    if (bytes.size() < 2) {
        return common::Status::InvalidArgument("Corrupted schema bytes: insufficient length for header");
    }

    std::size_t offset = 0;
    std::uint16_t col_count = 0;
    std::memcpy(&col_count, bytes.data() + offset, sizeof(col_count));
    offset += sizeof(col_count);

    std::vector<Column> cols;
    cols.reserve(col_count);

    for (std::uint16_t i = 0; i < col_count; ++i) {
        if (offset + sizeof(std::uint16_t) > bytes.size()) {
            return common::Status::InvalidArgument("Corrupted schema bytes: unexpected EOF while reading name length");
        }
        std::uint16_t name_len = 0;
        std::memcpy(&name_len, bytes.data() + offset, sizeof(name_len));
        offset += sizeof(name_len);

        if (offset + name_len > bytes.size()) {
            return common::Status::InvalidArgument("Corrupted schema bytes: unexpected EOF while reading column name");
        }
        std::string name(reinterpret_cast<const char*>(bytes.data() + offset), name_len);
        offset += name_len;

        // Needs: type (1) + is_pk (1) + is_nullable (1) + length (4) = 7 bytes
        if (offset + 7 > bytes.size()) {
            return common::Status::InvalidArgument("Corrupted schema bytes: unexpected EOF while reading column metadata");
        }

        const auto type_val = bytes[offset++];
        if (type_val > static_cast<std::uint8_t>(DataType::VARCHAR)) {
            return common::Status::InvalidArgument("Corrupted schema bytes: unknown data type code");
        }
        const auto type = static_cast<DataType>(type_val);

        const bool is_pk = (bytes[offset++] != 0);
        const bool is_nullable = (bytes[offset++] != 0);

        std::uint32_t col_length = 0;
        std::memcpy(&col_length, bytes.data() + offset, sizeof(col_length));
        offset += sizeof(col_length);

        cols.emplace_back(std::move(name), type, is_pk, is_nullable, col_length);
    }

    out_schema = Schema(std::move(cols));
    return common::Status::Ok();
}

} // namespace querycraft::storage
