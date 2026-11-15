#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace querycraft {

// QueryCraft Version Information
inline constexpr std::string_view QUERYCRAFT_VERSION = "0.1.0-alpha";
inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

// Fundamental Database Engine Parameters
// Standard 4KB page size, commonly used in relational engines (SQLite, Postgres, BusTub)
inline constexpr std::size_t PAGE_SIZE = 4096;

// Core Type Identifiers
using page_id_t = std::int32_t;
using frame_id_t = std::int32_t;
using txn_id_t = std::int64_t;
using lsn_t = std::int64_t;
using oid_t = std::uint32_t;

// Sentinel Values
inline constexpr page_id_t INVALID_PAGE_ID = -1;
inline constexpr frame_id_t INVALID_FRAME_ID = -1;
inline constexpr txn_id_t INVALID_TXN_ID = -1;
inline constexpr lsn_t INVALID_LSN = -1;

} // namespace querycraft
