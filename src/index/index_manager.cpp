#include "index/index_manager.hpp"

#include <cctype>
#include <cstring>

namespace querycraft::index {

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

inline void append_int32(std::vector<std::uint8_t>& buf, std::int32_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(val));
}

inline void append_uint32(std::vector<std::uint8_t>& buf, std::uint32_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(val));
}

} // namespace

IndexManager::IndexManager(storage::DiskManager& dm) : disk_manager_(dm) {}

common::Status IndexManager::create_index(const std::string& index_name,
                                         const std::string& table_name,
                                         const std::string& column_name,
                                         KeyType key_type,
                                         std::size_t max_keys) {
    const auto key = to_lower_ascii(index_name);
    if (indexes_.find(key) != indexes_.end()) {
        return common::Status::AlreadyExists("Index '" + index_name + "' already exists");
    }

    if (has_index(table_name, column_name)) {
        return common::Status::AlreadyExists(
            "An index already exists on table '" + table_name + "', column '" + column_name + "'");
    }

    IndexMetadata meta{
        .index_name = index_name,
        .table_name = table_name,
        .column_name = column_name,
        .key_type = key_type,
        .root_page_id = INVALID_PAGE_ID,
        .max_keys = max_keys
    };

    auto tree = std::make_unique<BPlusTree>(max_keys);
    indexes_.emplace(key, IndexEntry{std::move(meta), std::move(tree)});

    return common::Status::Ok();
}

common::Status IndexManager::drop_index(std::string_view index_name) {
    const auto it = indexes_.find(to_lower_ascii(index_name));
    if (it == indexes_.end()) {
        return common::Status::NotFound("Index '" + std::string(index_name) + "' not found");
    }
    indexes_.erase(it);
    return common::Status::Ok();
}

bool IndexManager::has_index(std::string_view index_name) const {
    return indexes_.find(to_lower_ascii(index_name)) != indexes_.end();
}

bool IndexManager::has_index(std::string_view table_name, std::string_view column_name) const {
    const auto t_target = to_lower_ascii(table_name);
    const auto c_target = to_lower_ascii(column_name);
    for (const auto& [_, entry] : indexes_) {
        if (to_lower_ascii(entry.metadata.table_name) == t_target &&
            to_lower_ascii(entry.metadata.column_name) == c_target) {
            return true;
        }
    }
    return false;
}

BPlusTree* IndexManager::get_index(std::string_view index_name) {
    const auto it = indexes_.find(to_lower_ascii(index_name));
    if (it == indexes_.end()) {
        return nullptr;
    }
    return it->second.tree.get();
}

const BPlusTree* IndexManager::get_index(std::string_view index_name) const {
    const auto it = indexes_.find(to_lower_ascii(index_name));
    if (it == indexes_.end()) {
        return nullptr;
    }
    return it->second.tree.get();
}

BPlusTree* IndexManager::get_index(std::string_view table_name, std::string_view column_name) {
    const auto t_target = to_lower_ascii(table_name);
    const auto c_target = to_lower_ascii(column_name);
    for (auto& [_, entry] : indexes_) {
        if (to_lower_ascii(entry.metadata.table_name) == t_target &&
            to_lower_ascii(entry.metadata.column_name) == c_target) {
            return entry.tree.get();
        }
    }
    return nullptr;
}

const BPlusTree* IndexManager::get_index(std::string_view table_name, std::string_view column_name) const {
    const auto t_target = to_lower_ascii(table_name);
    const auto c_target = to_lower_ascii(column_name);
    for (const auto& [_, entry] : indexes_) {
        if (to_lower_ascii(entry.metadata.table_name) == t_target &&
            to_lower_ascii(entry.metadata.column_name) == c_target) {
            return entry.tree.get();
        }
    }
    return nullptr;
}

const IndexMetadata* IndexManager::get_metadata(std::string_view index_name) const {
    const auto it = indexes_.find(to_lower_ascii(index_name));
    if (it == indexes_.end()) {
        return nullptr;
    }
    return &it->second.metadata;
}

std::vector<std::string> IndexManager::list_indexes() const {
    std::vector<std::string> names;
    names.reserve(indexes_.size());
    for (const auto& [_, entry] : indexes_) {
        names.push_back(entry.metadata.index_name);
    }
    return names;
}

std::vector<std::string> IndexManager::list_table_indexes(std::string_view table_name) const {
    const auto target = to_lower_ascii(table_name);
    std::vector<std::string> names;
    for (const auto& [_, entry] : indexes_) {
        if (to_lower_ascii(entry.metadata.table_name) == target) {
            names.push_back(entry.metadata.index_name);
        }
    }
    return names;
}

common::Status IndexManager::insert_entry(std::string_view index_name,
                                         const IndexKey& key,
                                         storage::RecordId rid) {
    const auto it = indexes_.find(to_lower_ascii(index_name));
    if (it == indexes_.end()) {
        return common::Status::NotFound("Index '" + std::string(index_name) + "' not found");
    }

    if (!key.is_null() && key.type() != it->second.metadata.key_type) {
        return common::Status::InvalidArgument(
            "Key type mismatch for index '" + std::string(index_name) + "': expected " +
            std::string(key_type_to_string(it->second.metadata.key_type)) + ", got " +
            std::string(key_type_to_string(key.type())));
    }

    return it->second.tree->insert(key, rid);
}

common::Status IndexManager::delete_entry(std::string_view index_name,
                                         const IndexKey& key,
                                         storage::RecordId rid) {
    const auto it = indexes_.find(to_lower_ascii(index_name));
    if (it == indexes_.end()) {
        return common::Status::NotFound("Index '" + std::string(index_name) + "' not found");
    }
    return it->second.tree->delete_entry(key, rid);
}

std::vector<storage::RecordId> IndexManager::search(std::string_view index_name,
                                                    const IndexKey& key) const {
    const auto* tree = get_index(index_name);
    if (!tree) {
        return {};
    }
    return tree->search(key);
}

std::vector<storage::RecordId> IndexManager::range_search(std::string_view index_name,
                                                          const IndexKey& lower,
                                                          const IndexKey& upper,
                                                          bool lower_inc,
                                                          bool upper_inc) const {
    const auto* tree = get_index(index_name);
    if (!tree) {
        return {};
    }
    return tree->range_search_records(lower, upper, lower_inc, upper_inc);
}

common::Status IndexManager::persist(page_id_t& out_catalog_pid) {
    if (!disk_manager_.is_open()) {
        return common::Status::IoError("DiskManager is not open");
    }

    // Persist all individual B+ trees
    for (auto& [_, entry] : indexes_) {
        auto status = entry.tree->persist(disk_manager_, entry.metadata.root_page_id);
        if (!status.ok()) {
            return status;
        }
    }

    if (catalog_page_id_ == INVALID_PAGE_ID) {
        page_id_t new_pid = INVALID_PAGE_ID;
        auto alloc_status = disk_manager_.allocate_page(new_pid);
        if (!alloc_status.ok()) {
            return alloc_status;
        }
        catalog_page_id_ = new_pid;
    }

    storage::Page catalog_page(catalog_page_id_, storage::PageType::INDEX_PAGE);
    auto* buf = catalog_page.data();

    // Catalog page header:
    // [0..3]: Magic 0x51434958 ("QCIX")
    // [4..5]: Version 1
    // [6..7]: Number of indexes
    std::memcpy(buf + 0, &INDEX_CATALOG_MAGIC, sizeof(INDEX_CATALOG_MAGIC));
    std::memcpy(buf + 4, &INDEX_CATALOG_VERSION, sizeof(INDEX_CATALOG_VERSION));
    const auto count = static_cast<std::uint16_t>(indexes_.size());
    std::memcpy(buf + 6, &count, sizeof(count));

    std::size_t offset = 8;
    for (const auto& [_, entry] : indexes_) {
        const auto& meta = entry.metadata;

        const auto idx_len = static_cast<std::uint16_t>(meta.index_name.size());
        const auto tbl_len = static_cast<std::uint16_t>(meta.table_name.size());
        const auto col_len = static_cast<std::uint16_t>(meta.column_name.size());

        const auto needed = sizeof(idx_len) + idx_len +
                            sizeof(tbl_len) + tbl_len +
                            sizeof(col_len) + col_len +
                            sizeof(std::uint8_t) +
                            sizeof(meta.root_page_id) +
                            sizeof(std::uint32_t);

        if (offset + needed > PAGE_SIZE) {
            return common::Status::InternalError("Index catalog capacity exceeded 4KB page size");
        }

        std::memcpy(buf + offset, &idx_len, sizeof(idx_len));
        offset += sizeof(idx_len);
        std::memcpy(buf + offset, meta.index_name.data(), idx_len);
        offset += idx_len;

        std::memcpy(buf + offset, &tbl_len, sizeof(tbl_len));
        offset += sizeof(tbl_len);
        std::memcpy(buf + offset, meta.table_name.data(), tbl_len);
        offset += tbl_len;

        std::memcpy(buf + offset, &col_len, sizeof(col_len));
        offset += sizeof(col_len);
        std::memcpy(buf + offset, meta.column_name.data(), col_len);
        offset += col_len;

        const auto ktype = static_cast<std::uint8_t>(meta.key_type);
        buf[offset++] = ktype;

        std::memcpy(buf + offset, &meta.root_page_id, sizeof(meta.root_page_id));
        offset += sizeof(meta.root_page_id);

        const auto max_k = static_cast<std::uint32_t>(meta.max_keys);
        std::memcpy(buf + offset, &max_k, sizeof(max_k));
        offset += sizeof(max_k);
    }

    auto write_status = disk_manager_.write_page(catalog_page_id_, catalog_page);
    if (!write_status.ok()) {
        return write_status;
    }

    auto flush_status = disk_manager_.flush();
    if (!flush_status.ok()) {
        return flush_status;
    }

    out_catalog_pid = catalog_page_id_;
    return common::Status::Ok();
}

common::Status IndexManager::load(page_id_t catalog_pid) {
    if (!disk_manager_.is_open()) {
        return common::Status::IoError("DiskManager is not open");
    }

    if (catalog_pid == INVALID_PAGE_ID) {
        indexes_.clear();
        catalog_page_id_ = INVALID_PAGE_ID;
        return common::Status::Ok();
    }

    storage::Page catalog_page;
    auto read_status = disk_manager_.read_page(catalog_pid, catalog_page);
    if (!read_status.ok()) {
        return read_status;
    }

    const auto* buf = catalog_page.data();

    std::uint32_t magic = 0;
    std::memcpy(&magic, buf + 0, sizeof(magic));
    if (magic != INDEX_CATALOG_MAGIC) {
        return common::Status::InternalError("Corrupted index catalog: invalid magic signature");
    }

    std::uint16_t version = 0;
    std::memcpy(&version, buf + 4, sizeof(version));
    if (version != INDEX_CATALOG_VERSION) {
        return common::Status::InternalError("Unsupported index catalog version: " + std::to_string(version));
    }

    std::uint16_t count = 0;
    std::memcpy(&count, buf + 6, sizeof(count));

    std::size_t offset = 8;
    indexes_.clear();

    for (std::uint16_t i = 0; i < count; ++i) {
        if (offset + sizeof(std::uint16_t) > PAGE_SIZE) {
            return common::Status::InternalError("Index catalog corrupted: unexpected EOF reading index name len");
        }
        std::uint16_t idx_len = 0;
        std::memcpy(&idx_len, buf + offset, sizeof(idx_len));
        offset += sizeof(idx_len);

        std::string index_name(reinterpret_cast<const char*>(buf + offset), idx_len);
        offset += idx_len;

        std::uint16_t tbl_len = 0;
        std::memcpy(&tbl_len, buf + offset, sizeof(tbl_len));
        offset += sizeof(tbl_len);
        std::string table_name(reinterpret_cast<const char*>(buf + offset), tbl_len);
        offset += tbl_len;

        std::uint16_t col_len = 0;
        std::memcpy(&col_len, buf + offset, sizeof(col_len));
        offset += sizeof(col_len);
        std::string column_name(reinterpret_cast<const char*>(buf + offset), col_len);
        offset += col_len;

        const auto ktype = static_cast<KeyType>(buf[offset++]);

        page_id_t root_pid = INVALID_PAGE_ID;
        std::memcpy(&root_pid, buf + offset, sizeof(root_pid));
        offset += sizeof(root_pid);

        std::uint32_t max_k = 4;
        std::memcpy(&max_k, buf + offset, sizeof(max_k));
        offset += sizeof(max_k);

        std::unique_ptr<BPlusTree> tree;
        auto load_tree_status = BPlusTree::load(disk_manager_, root_pid, max_k, tree);
        if (!load_tree_status.ok()) {
            return load_tree_status;
        }

        IndexMetadata meta{
            .index_name = index_name,
            .table_name = table_name,
            .column_name = column_name,
            .key_type = ktype,
            .root_page_id = root_pid,
            .max_keys = max_k
        };

        indexes_.emplace(to_lower_ascii(index_name), IndexEntry{std::move(meta), std::move(tree)});
    }

    catalog_page_id_ = catalog_pid;
    return common::Status::Ok();
}

} // namespace querycraft::index
