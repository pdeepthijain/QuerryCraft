#pragma once

#include "common/config.hpp"
#include "common/status.hpp"
#include "index/index_key.hpp"
#include "storage/page.hpp"
#include "storage/record.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace querycraft::index {

class BPlusTreeNode : public std::enable_shared_from_this<BPlusTreeNode> {
public:
    explicit BPlusTreeNode(bool is_leaf, std::size_t max_keys = 4, page_id_t page_id = INVALID_PAGE_ID);

    [[nodiscard]] bool is_leaf() const noexcept { return is_leaf_; }
    [[nodiscard]] std::size_t max_keys() const noexcept { return max_keys_; }
    [[nodiscard]] std::size_t min_keys() const noexcept {
        return is_leaf_ ? ((max_keys_ + 1) / 2) : (max_keys_ / 2);
    }
    [[nodiscard]] std::size_t key_count() const noexcept { return keys_.size(); }
    [[nodiscard]] page_id_t page_id() const noexcept { return page_id_; }
    void set_page_id(page_id_t pid) noexcept { page_id_ = pid; }

    [[nodiscard]] bool is_overflow() const noexcept { return keys_.size() > max_keys_; }
    [[nodiscard]] bool is_underflow(bool is_root) const noexcept;

    // Keys & Values access
    [[nodiscard]] const std::vector<IndexKey>& keys() const noexcept { return keys_; }
    [[nodiscard]] std::vector<IndexKey>& keys() noexcept { return keys_; }

    [[nodiscard]] const std::vector<std::vector<storage::RecordId>>& values() const noexcept { return values_; }
    [[nodiscard]] std::vector<std::vector<storage::RecordId>>& values() noexcept { return values_; }

    [[nodiscard]] const std::vector<std::shared_ptr<BPlusTreeNode>>& children() const noexcept { return children_; }
    [[nodiscard]] std::vector<std::shared_ptr<BPlusTreeNode>>& children() noexcept { return children_; }

    [[nodiscard]] std::shared_ptr<BPlusTreeNode> parent() const { return parent_.lock(); }
    void set_parent(std::shared_ptr<BPlusTreeNode> p) { parent_ = p; }

    [[nodiscard]] std::shared_ptr<BPlusTreeNode> next_leaf() const noexcept { return next_leaf_; }
    [[nodiscard]] std::shared_ptr<BPlusTreeNode> prev_leaf() const { return prev_leaf_.lock(); }
    void set_next_leaf(std::shared_ptr<BPlusTreeNode> next) noexcept { next_leaf_ = next; }
    void set_prev_leaf(std::shared_ptr<BPlusTreeNode> prev) { prev_leaf_ = prev; }

    [[nodiscard]] page_id_t next_page_id() const noexcept { return next_page_id_; }
    [[nodiscard]] page_id_t prev_page_id() const noexcept { return prev_page_id_; }
    void set_next_page_id(page_id_t pid) noexcept { next_page_id_ = pid; }
    void set_prev_page_id(page_id_t pid) noexcept { prev_page_id_ = pid; }

    // Search helpers
    [[nodiscard]] std::size_t find_key_index(const IndexKey& key) const;
    [[nodiscard]] std::size_t lower_bound_index(const IndexKey& key) const;
    [[nodiscard]] std::size_t find_child_index_for_key(const IndexKey& key) const;

    // Leaf operations
    void insert_leaf_record(const IndexKey& key, storage::RecordId rid);
    bool delete_leaf_record(const IndexKey& key, storage::RecordId rid);
    IndexKey split_leaf(std::shared_ptr<BPlusTreeNode> new_leaf);

    // Internal operations
    void insert_internal_child(const IndexKey& separator, std::shared_ptr<BPlusTreeNode> child);
    void split_internal(std::shared_ptr<BPlusTreeNode> new_internal, IndexKey& out_separator);

    // Disk page serialization
    common::Status serialize_to_page(storage::Page& page) const;
    static common::Status deserialize_from_page(const storage::Page& page,
                                                std::size_t max_keys,
                                                std::shared_ptr<BPlusTreeNode>& out_node,
                                                std::vector<page_id_t>& out_child_page_ids,
                                                page_id_t& out_parent_pid);

private:
    bool is_leaf_{true};
    std::size_t max_keys_{4};
    page_id_t page_id_{INVALID_PAGE_ID};

    // Sorted keys
    std::vector<IndexKey> keys_;

    // Leaf payload: values_[i] contains RecordIds for keys_[i]
    std::vector<std::vector<storage::RecordId>> values_;
    std::shared_ptr<BPlusTreeNode> next_leaf_{nullptr};
    std::weak_ptr<BPlusTreeNode> prev_leaf_{};
    page_id_t next_page_id_{INVALID_PAGE_ID};
    page_id_t prev_page_id_{INVALID_PAGE_ID};

    // Internal payload: children_.size() == keys_.size() + 1
    std::vector<std::shared_ptr<BPlusTreeNode>> children_;

    // Parent link
    std::weak_ptr<BPlusTreeNode> parent_{};
};

} // namespace querycraft::index
