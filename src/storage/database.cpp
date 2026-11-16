#include "storage/database.hpp"

#include <cctype>
#include <cstring>

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

} // namespace

Database::Database() = default;

Database::Database(const std::filesystem::path& db_file_path) {
    (void)open(db_file_path);
}

Database::~Database() {
    close();
}

common::Status Database::open(const std::filesystem::path& db_file_path) {
    close();
    db_path_ = db_file_path;

    const auto open_status = disk_manager_.open(db_file_path);
    if (!open_status.ok()) {
        return open_status;
    }

    if (disk_manager_.num_pages() == 0) {
        // Fresh database: allocate Page 0 as Catalog
        page_id_t cat_pid = INVALID_PAGE_ID;
        const auto alloc_status = disk_manager_.allocate_page(cat_pid);
        if (!alloc_status.ok()) {
            return alloc_status;
        }
        return flush_catalog();
    }

    // Existing database: restore catalog and tables
    return load_catalog();
}

void Database::close() {
    if (is_open()) {
        (void)flush_catalog();
        tables_.clear();
        disk_manager_.close();
    }
}

bool Database::is_open() const noexcept {
    return disk_manager_.is_open();
}

common::Status Database::create_table(const std::string& name, const Schema& schema) {
    if (!is_open()) {
        return common::Status::IoError("Database is not open");
    }

    const std::string key = to_lower_ascii(name);
    if (tables_.find(key) != tables_.end()) {
        return common::Status::AlreadyExists("Table '" + name + "' already exists");
    }

    // Allocate first data page for the new table
    page_id_t first_pid = INVALID_PAGE_ID;
    const auto alloc_status = disk_manager_.allocate_page(first_pid);
    if (!alloc_status.ok()) {
        return alloc_status;
    }

    Page initial_page(first_pid, PageType::TABLE_DATA_PAGE);
    const auto write_status = disk_manager_.write_page(first_pid, initial_page);
    if (!write_status.ok()) {
        return write_status;
    }

    auto table = std::make_unique<Table>(name, schema, disk_manager_, first_pid, first_pid);
    tables_.emplace(key, std::move(table));

    return flush_catalog();
}

bool Database::has_table(std::string_view name) const {
    return tables_.find(to_lower_ascii(name)) != tables_.end();
}

Table* Database::get_table(std::string_view name) {
    const auto it = tables_.find(to_lower_ascii(name));
    if (it == tables_.end()) {
        return nullptr;
    }
    return it->second.get();
}

const Table* Database::get_table(std::string_view name) const {
    const auto it = tables_.find(to_lower_ascii(name));
    if (it == tables_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<std::string> Database::list_tables() const {
    std::vector<std::string> result;
    result.reserve(tables_.size());
    for (const auto& [_, table_ptr] : tables_) {
        result.push_back(table_ptr->name());
    }
    return result;
}

common::Status Database::insert_record(const std::string& table_name, Record& record, RecordId& out_rid) {
    auto* tbl = get_table(table_name);
    if (!tbl) {
        return common::Status::NotFound("Table '" + table_name + "' does not exist");
    }
    const auto prev_last = tbl->last_page_id();
    const auto status = tbl->insert_record(record, out_rid);
    if (status.ok() && tbl->last_page_id() != prev_last) {
        // Last page was expanded, keep catalog metadata updated
        (void)flush_catalog();
    }
    return status;
}

common::Status Database::get_record(const std::string& table_name, RecordId rid, Record& out_record) const {
    const auto* tbl = get_table(table_name);
    if (!tbl) {
        return common::Status::NotFound("Table '" + table_name + "' does not exist");
    }
    return tbl->get_record(rid, out_record);
}

common::Status Database::update_record(const std::string& table_name, RecordId rid, const Record& new_record) {
    auto* tbl = get_table(table_name);
    if (!tbl) {
        return common::Status::NotFound("Table '" + table_name + "' does not exist");
    }
    return tbl->update_record(rid, new_record);
}

common::Status Database::delete_record(const std::string& table_name, RecordId rid) {
    auto* tbl = get_table(table_name);
    if (!tbl) {
        return common::Status::NotFound("Table '" + table_name + "' does not exist");
    }
    return tbl->delete_record(rid);
}

common::Status Database::scan_table(const std::string& table_name, std::vector<Record>& out_records) const {
    const auto* tbl = get_table(table_name);
    if (!tbl) {
        return common::Status::NotFound("Table '" + table_name + "' does not exist");
    }
    return tbl->scan_records(out_records);
}

common::Status Database::flush_catalog() {
    if (!disk_manager_.is_open()) {
        return common::Status::IoError("Database file is not open");
    }

    Page catalog_page(0, PageType::CATALOG_PAGE);
    auto* buf = catalog_page.data();

    // Fixed Catalog Page Header:
    // [0..3]: Magic (0x51435246)
    // [4..5]: Version (1)
    // [6..7]: Number of tables
    std::memcpy(buf + 0, &CATALOG_MAGIC, sizeof(CATALOG_MAGIC));
    std::memcpy(buf + 4, &CATALOG_VERSION, sizeof(CATALOG_VERSION));
    const auto table_count = static_cast<std::uint16_t>(tables_.size());
    std::memcpy(buf + 6, &table_count, sizeof(table_count));

    std::size_t offset = 8;
    for (const auto& [_, table_ptr] : tables_) {
        const std::string& name = table_ptr->name();
        const auto schema_bytes = table_ptr->schema().serialize();

        const auto needed = sizeof(std::uint16_t) + name.size() +
                            sizeof(page_id_t) * 2 +
                            sizeof(std::uint16_t) + schema_bytes.size();

        if (offset + needed > PAGE_SIZE) {
            return common::Status::InternalError("Catalog page capacity exceeded: too many tables/columns");
        }

        // Table name length & data
        const auto name_len = static_cast<std::uint16_t>(name.size());
        std::memcpy(buf + offset, &name_len, sizeof(name_len));
        offset += sizeof(name_len);
        std::memcpy(buf + offset, name.data(), name_len);
        offset += name_len;

        // First and last data page IDs
        const page_id_t first_pid = table_ptr->first_page_id();
        const page_id_t last_pid = table_ptr->last_page_id();
        std::memcpy(buf + offset, &first_pid, sizeof(first_pid));
        offset += sizeof(first_pid);
        std::memcpy(buf + offset, &last_pid, sizeof(last_pid));
        offset += sizeof(last_pid);

        // Schema serialized length & data
        const auto schema_len = static_cast<std::uint16_t>(schema_bytes.size());
        std::memcpy(buf + offset, &schema_len, sizeof(schema_len));
        offset += sizeof(schema_len);
        std::memcpy(buf + offset, schema_bytes.data(), schema_len);
        offset += schema_len;
    }

    const auto write_status = disk_manager_.write_page(0, catalog_page);
    if (!write_status.ok()) {
        return write_status;
    }

    return disk_manager_.flush();
}

common::Status Database::load_catalog() {
    Page catalog_page;
    const auto read_status = disk_manager_.read_page(0, catalog_page);
    if (!read_status.ok()) {
        return read_status;
    }

    const auto* buf = catalog_page.data();

    std::uint32_t magic = 0;
    std::memcpy(&magic, buf + 0, sizeof(magic));
    if (magic != CATALOG_MAGIC) {
        return common::Status::InternalError("Corrupted database file: invalid catalog magic signature");
    }

    std::uint16_t version = 0;
    std::memcpy(&version, buf + 4, sizeof(version));
    if (version != CATALOG_VERSION) {
        return common::Status::InternalError("Unsupported catalog version: " + std::to_string(version));
    }

    std::uint16_t table_count = 0;
    std::memcpy(&table_count, buf + 6, sizeof(table_count));

    std::size_t offset = 8;
    tables_.clear();

    for (std::uint16_t i = 0; i < table_count; ++i) {
        if (offset + sizeof(std::uint16_t) > PAGE_SIZE) {
            return common::Status::InternalError("Catalog corrupted: unexpected EOF reading table name length");
        }

        std::uint16_t name_len = 0;
        std::memcpy(&name_len, buf + offset, sizeof(name_len));
        offset += sizeof(name_len);

        if (offset + name_len + sizeof(page_id_t) * 2 + sizeof(std::uint16_t) > PAGE_SIZE) {
            return common::Status::InternalError("Catalog corrupted: unexpected EOF reading table header");
        }

        std::string table_name(reinterpret_cast<const char*>(buf + offset), name_len);
        offset += name_len;

        page_id_t first_pid = INVALID_PAGE_ID;
        page_id_t last_pid = INVALID_PAGE_ID;
        std::memcpy(&first_pid, buf + offset, sizeof(first_pid));
        offset += sizeof(first_pid);
        std::memcpy(&last_pid, buf + offset, sizeof(last_pid));
        offset += sizeof(last_pid);

        std::uint16_t schema_len = 0;
        std::memcpy(&schema_len, buf + offset, sizeof(schema_len));
        offset += sizeof(schema_len);

        if (offset + schema_len > PAGE_SIZE) {
            return common::Status::InternalError("Catalog corrupted: unexpected EOF reading schema payload");
        }

        Schema schema;
        const auto schema_status = Schema::deserialize(
            std::span<const std::uint8_t>(buf + offset, schema_len), schema);
        if (!schema_status.ok()) {
            return schema_status;
        }
        offset += schema_len;

        auto table = std::make_unique<Table>(
            table_name, std::move(schema), disk_manager_, first_pid, last_pid);
        tables_.emplace(to_lower_ascii(table_name), std::move(table));
    }

    return common::Status::Ok();
}

} // namespace querycraft::storage
