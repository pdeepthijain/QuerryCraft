#pragma once

#include "common/status.hpp"
#include "storage/disk_manager.hpp"
#include "storage/record.hpp"
#include "storage/schema.hpp"
#include "storage/table.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace querycraft::storage {

inline constexpr std::uint32_t CATALOG_MAGIC = 0x51435246; // "QCRF"
inline constexpr std::uint16_t CATALOG_VERSION = 1;

class Database {
public:
    Database();
    explicit Database(const std::filesystem::path& db_file_path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    Database(Database&&) noexcept = default;
    Database& operator=(Database&&) noexcept = default;

    // Open existing or create new database file
    common::Status open(const std::filesystem::path& db_file_path);
    void close();
    [[nodiscard]] bool is_open() const noexcept;

    // Table Management
    common::Status create_table(const std::string& name, const Schema& schema);
    [[nodiscard]] bool has_table(std::string_view name) const;
    [[nodiscard]] Table* get_table(std::string_view name);
    [[nodiscard]] const Table* get_table(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> list_tables() const;

    // High-level Record Operations
    common::Status insert_record(const std::string& table_name, Record& record, RecordId& out_rid);
    common::Status get_record(const std::string& table_name, RecordId rid, Record& out_record) const;
    common::Status update_record(const std::string& table_name, RecordId rid, const Record& new_record);
    common::Status delete_record(const std::string& table_name, RecordId rid);
    common::Status scan_table(const std::string& table_name, std::vector<Record>& out_records) const;

    // Catalog persistence
    common::Status flush_catalog();

    // Access low-level disk manager
    DiskManager& disk_manager() noexcept { return disk_manager_; }
    const DiskManager& disk_manager() const noexcept { return disk_manager_; }

private:
    common::Status load_catalog();

    std::filesystem::path db_path_;
    DiskManager disk_manager_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
};

} // namespace querycraft::storage
