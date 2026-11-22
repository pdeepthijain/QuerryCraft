#pragma once

#include "common/config.hpp"
#include "common/status.hpp"
#include "index/bplus_tree.hpp"
#include "index/index_key.hpp"
#include "storage/disk_manager.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace querycraft::buffer {
class BufferPoolManager;
}

namespace querycraft::index {

inline constexpr std::uint32_t INDEX_CATALOG_MAGIC = 0x51434958; // "QCIX"
inline constexpr std::uint16_t INDEX_CATALOG_VERSION = 1;

struct IndexMetadata {
    std::string index_name;
    std::string table_name;
    std::string column_name;
    KeyType key_type{KeyType::INT};
    page_id_t root_page_id{INVALID_PAGE_ID};
    std::size_t max_keys{4};

    bool operator==(const IndexMetadata& other) const = default;
};

class IndexManager {
public:
    explicit IndexManager(storage::DiskManager& dm);
    ~IndexManager() = default;

    IndexManager(const IndexManager&) = delete;
    IndexManager& operator=(const IndexManager&) = delete;

    IndexManager(IndexManager&&) noexcept = default;
    IndexManager& operator=(IndexManager&&) noexcept = default;

    // Index creation and removal
    common::Status create_index(const std::string& index_name,
                                const std::string& table_name,
                                const std::string& column_name,
                                KeyType key_type,
                                std::size_t max_keys = 4);

    common::Status drop_index(std::string_view index_name);

    // Queries
    [[nodiscard]] bool has_index(std::string_view index_name) const;
    [[nodiscard]] bool has_index(std::string_view table_name, std::string_view column_name) const;

    [[nodiscard]] BPlusTree* get_index(std::string_view index_name);
    [[nodiscard]] const BPlusTree* get_index(std::string_view index_name) const;

    [[nodiscard]] BPlusTree* get_index(std::string_view table_name, std::string_view column_name);
    [[nodiscard]] const BPlusTree* get_index(std::string_view table_name, std::string_view column_name) const;

    [[nodiscard]] const IndexMetadata* get_metadata(std::string_view index_name) const;
    [[nodiscard]] std::vector<std::string> list_indexes() const;
    [[nodiscard]] std::vector<std::string> list_table_indexes(std::string_view table_name) const;

    // Index Operations
    common::Status insert_entry(std::string_view index_name, const IndexKey& key, storage::RecordId rid);
    common::Status delete_entry(std::string_view index_name, const IndexKey& key, storage::RecordId rid);
    [[nodiscard]] std::vector<storage::RecordId> search(std::string_view index_name, const IndexKey& key) const;
    [[nodiscard]] std::vector<storage::RecordId> range_search(std::string_view index_name,
                                                             const IndexKey& lower,
                                                             const IndexKey& upper,
                                                             bool lower_inc = true,
                                                             bool upper_inc = true) const;

    // Persistence across application restarts
    common::Status persist(page_id_t& out_catalog_pid);
    common::Status load(page_id_t catalog_pid);
    common::Status auto_load();
    common::Status persist(buffer::BufferPoolManager& bpm, page_id_t& out_catalog_pid);
    common::Status load(buffer::BufferPoolManager& bpm, page_id_t catalog_pid);
    [[nodiscard]] page_id_t catalog_page_id() const noexcept { return catalog_page_id_; }

private:
    struct IndexEntry {
        IndexMetadata metadata;
        std::unique_ptr<BPlusTree> tree;
    };

    storage::DiskManager& disk_manager_;
    page_id_t catalog_page_id_{INVALID_PAGE_ID};
    std::unordered_map<std::string, IndexEntry> indexes_;
};

} // namespace querycraft::index
