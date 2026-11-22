#pragma once

#include "common/config.hpp"
#include "common/status.hpp"
#include "index/bplus_tree_node.hpp"
#include "index/index_key.hpp"
#include "storage/disk_manager.hpp"
#include "storage/record.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace querycraft::buffer {
class BufferPoolManager;
}

namespace querycraft::index {

class BPlusTree {
public:
    explicit BPlusTree(std::size_t max_keys = 4);
    ~BPlusTree() = default;

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    BPlusTree(BPlusTree&&) noexcept = default;
    BPlusTree& operator=(BPlusTree&&) noexcept = default;

    // Core operations
    common::Status insert(const IndexKey& key, storage::RecordId rid);
    [[nodiscard]] std::vector<storage::RecordId> search(const IndexKey& key) const;
    common::Status delete_entry(const IndexKey& key, storage::RecordId rid);

    // Range search: returns all (key, RecordIds) within bounds
    [[nodiscard]] std::vector<std::pair<IndexKey, std::vector<storage::RecordId>>> range_search(
        const IndexKey& lower,
        const IndexKey& upper,
        bool lower_inclusive = true,
        bool upper_inclusive = true) const;

    // Range search returning flattened RecordIds
    [[nodiscard]] std::vector<storage::RecordId> range_search_records(
        const IndexKey& lower,
        const IndexKey& upper,
        bool lower_inclusive = true,
        bool upper_inclusive = true) const;

    // Ordered traversal of all keys and their RecordIds
    [[nodiscard]] std::vector<std::pair<IndexKey, std::vector<storage::RecordId>>> ordered_traversal() const;

    [[nodiscard]] bool is_empty() const noexcept {
        return root_ == nullptr || (root_->is_leaf() && root_->key_count() == 0);
    }
    [[nodiscard]] std::size_t max_keys() const noexcept { return max_keys_; }
    [[nodiscard]] std::shared_ptr<BPlusTreeNode> root() const noexcept { return root_; }
    [[nodiscard]] page_id_t root_page_id() const noexcept { return root_ ? root_->page_id() : INVALID_PAGE_ID; }

    // Tree Invariant Validator
    [[nodiscard]] common::Status validate_tree() const;

    // Persistence with DiskManager
    common::Status persist(storage::DiskManager& dm, page_id_t& out_root_pid);
    static common::Status load(storage::DiskManager& dm,
                               page_id_t root_pid,
                               std::size_t max_keys,
                               std::unique_ptr<BPlusTree>& out_tree);

    // Persistence with BufferPoolManager
    common::Status persist(buffer::BufferPoolManager& bpm, page_id_t& out_root_pid);
    static common::Status load(buffer::BufferPoolManager& bpm,
                               page_id_t root_pid,
                               std::size_t max_keys,
                               std::unique_ptr<BPlusTree>& out_tree);

private:
    [[nodiscard]] std::shared_ptr<BPlusTreeNode> find_leaf_node(const IndexKey& key) const;
    [[nodiscard]] std::shared_ptr<BPlusTreeNode> find_leftmost_leaf() const;

    void insert_into_parent(std::shared_ptr<BPlusTreeNode> left,
                            const IndexKey& separator,
                            std::shared_ptr<BPlusTreeNode> right);

    void handle_underflow(std::shared_ptr<BPlusTreeNode> node);

    std::size_t max_keys_{4};
    std::shared_ptr<BPlusTreeNode> root_{nullptr};
};

} // namespace querycraft::index
