#include "index/bplus_tree_node.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace querycraft::index {

namespace {

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

BPlusTreeNode::BPlusTreeNode(bool is_leaf, std::size_t max_keys, page_id_t page_id)
    : is_leaf_(is_leaf), max_keys_(max_keys), page_id_(page_id) {
    keys_.reserve(max_keys_ + 1);
    if (is_leaf_) {
        values_.reserve(max_keys_ + 1);
    } else {
        children_.reserve(max_keys_ + 2);
    }
}

bool BPlusTreeNode::is_underflow(bool is_root) const noexcept {
    if (is_root) {
        return keys_.empty();
    }
    return keys_.size() < min_keys();
}

std::size_t BPlusTreeNode::lower_bound_index(const IndexKey& key) const {
    auto it = std::lower_bound(
        keys_.begin(), keys_.end(), key,
        [](const IndexKey& a, const IndexKey& b) {
            return a < b;
        });
    return static_cast<std::size_t>(std::distance(keys_.begin(), it));
}

std::size_t BPlusTreeNode::find_key_index(const IndexKey& key) const {
    const auto idx = lower_bound_index(key);
    if (idx < keys_.size() && keys_[idx] == key) {
        return idx;
    }
    return keys_.size();
}

std::size_t BPlusTreeNode::find_child_index_for_key(const IndexKey& key) const {
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        if (key < keys_[i]) {
            return i;
        }
    }
    return keys_.size();
}

void BPlusTreeNode::insert_leaf_record(const IndexKey& key, storage::RecordId rid) {
    if (!is_leaf_) {
        throw std::logic_error("insert_leaf_record called on internal node");
    }

    const auto idx = lower_bound_index(key);
    if (idx < keys_.size() && keys_[idx] == key) {
        // Duplicate key: append rid if not already present
        auto& rids = values_[idx];
        if (std::find(rids.begin(), rids.end(), rid) == rids.end()) {
            rids.push_back(rid);
        }
    } else {
        keys_.insert(keys_.begin() + static_cast<std::ptrdiff_t>(idx), key);
        values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(idx), std::vector<storage::RecordId>{rid});
    }
}

bool BPlusTreeNode::delete_leaf_record(const IndexKey& key, storage::RecordId rid) {
    if (!is_leaf_) {
        throw std::logic_error("delete_leaf_record called on internal node");
    }

    const auto idx = find_key_index(key);
    if (idx == keys_.size()) {
        return false;
    }

    auto& rids = values_[idx];
    auto it = std::find(rids.begin(), rids.end(), rid);
    if (it == rids.end()) {
        return false;
    }

    rids.erase(it);
    if (rids.empty()) {
        keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(idx));
        values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(idx));
    }
    return true;
}

IndexKey BPlusTreeNode::split_leaf(std::shared_ptr<BPlusTreeNode> new_leaf) {
    if (!is_leaf_ || !new_leaf->is_leaf_) {
        throw std::logic_error("split_leaf called on non-leaf node");
    }

    const auto split_idx = static_cast<std::ptrdiff_t>(keys_.size() / 2);

    new_leaf->keys_.assign(keys_.begin() + split_idx, keys_.end());
    new_leaf->values_.assign(values_.begin() + split_idx, values_.end());

    keys_.erase(keys_.begin() + split_idx, keys_.end());
    values_.erase(values_.begin() + split_idx, values_.end());

    // Update leaf pointers
    new_leaf->next_leaf_ = next_leaf_;
    new_leaf->prev_leaf_ = shared_from_this();
    new_leaf->next_page_id_ = next_page_id_;
    new_leaf->prev_page_id_ = page_id_;

    if (next_leaf_) {
        next_leaf_->prev_leaf_ = new_leaf;
        next_leaf_->prev_page_id_ = new_leaf->page_id_;
    }
    next_leaf_ = new_leaf;
    next_page_id_ = new_leaf->page_id_;

    return new_leaf->keys_[0];
}

void BPlusTreeNode::insert_internal_child(const IndexKey& separator, std::shared_ptr<BPlusTreeNode> child) {
    if (is_leaf_) {
        throw std::logic_error("insert_internal_child called on leaf node");
    }

    const auto idx = static_cast<std::ptrdiff_t>(lower_bound_index(separator));
    keys_.insert(keys_.begin() + idx, separator);
    children_.insert(children_.begin() + idx + 1, child);
    child->set_parent(shared_from_this());
}

void BPlusTreeNode::split_internal(std::shared_ptr<BPlusTreeNode> new_internal, IndexKey& out_separator) {
    if (is_leaf_ || new_internal->is_leaf_) {
        throw std::logic_error("split_internal called on leaf node");
    }

    const auto mid = static_cast<std::ptrdiff_t>(keys_.size() / 2);
    out_separator = keys_[static_cast<std::size_t>(mid)];

    new_internal->keys_.assign(keys_.begin() + mid + 1, keys_.end());
    new_internal->children_.assign(children_.begin() + mid + 1, children_.end());

    for (const auto& child : new_internal->children_) {
        child->set_parent(new_internal);
    }

    keys_.erase(keys_.begin() + mid, keys_.end());
    children_.erase(children_.begin() + mid + 1, children_.end());
}

common::Status BPlusTreeNode::serialize_to_page(storage::Page& page) const {
    page.reset(page_id_, storage::PageType::INDEX_PAGE);
    page.set_prev_page_id(prev_page_id_);
    page.set_next_page_id(next_page_id_);

    std::vector<std::uint8_t> payload;
    // Format:
    // [0]: uint8_t is_leaf (1 or 0)
    // [1..2]: uint16_t num_keys
    // [3..6]: int32_t parent_page_id
    payload.push_back(is_leaf_ ? 1 : 0);
    append_uint16(payload, static_cast<std::uint16_t>(keys_.size()));
    const auto p = parent();
    append_int32(payload, p ? p->page_id() : INVALID_PAGE_ID);

    if (is_leaf_) {
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            keys_[i].serialize(payload);
            const auto& rids = values_[i];
            append_uint16(payload, static_cast<std::uint16_t>(rids.size()));
            for (const auto& rid : rids) {
                append_int32(payload, rid.page_id);
                append_uint32(payload, rid.slot_id);
            }
        }
    } else {
        // Internal node: child 0 page ID, then for each key: key + child (i+1) page ID
        const auto c0_pid = children_.empty() ? INVALID_PAGE_ID : children_[0]->page_id();
        append_int32(payload, c0_pid);
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            keys_[i].serialize(payload);
            const auto c_pid = (i + 1 < children_.size()) ? children_[i + 1]->page_id() : INVALID_PAGE_ID;
            append_int32(payload, c_pid);
        }
    }

    if (storage::PAGE_HEADER_SIZE + payload.size() > PAGE_SIZE) {
        return common::Status::InternalError("BPlusTreeNode payload exceeds 4KB page size");
    }

    return page.write_bytes(storage::PAGE_HEADER_SIZE, payload);
}

common::Status BPlusTreeNode::deserialize_from_page(const storage::Page& page,
                                                   std::size_t max_keys,
                                                   std::shared_ptr<BPlusTreeNode>& out_node,
                                                   std::vector<page_id_t>& out_child_page_ids,
                                                   page_id_t& out_parent_pid) {
    if (page.page_type() != storage::PageType::INDEX_PAGE) {
        return common::Status::InvalidArgument("Page is not an INDEX_PAGE");
    }

    std::span<const std::uint8_t> page_bytes(page.data(), PAGE_SIZE);
    std::size_t offset = storage::PAGE_HEADER_SIZE;

    if (offset + 7 > PAGE_SIZE) {
        return common::Status::InvalidArgument("Corrupted index page: insufficient length for node header");
    }

    const bool is_leaf = (page_bytes[offset++] != 0);

    std::uint16_t num_keys = 0;
    std::memcpy(&num_keys, page_bytes.data() + offset, sizeof(num_keys));
    offset += sizeof(num_keys);

    page_id_t parent_pid = INVALID_PAGE_ID;
    std::memcpy(&parent_pid, page_bytes.data() + offset, sizeof(parent_pid));
    offset += sizeof(parent_pid);
    out_parent_pid = parent_pid;

    auto node = std::make_shared<BPlusTreeNode>(is_leaf, max_keys, page.page_id());
    node->set_prev_page_id(page.prev_page_id());
    node->set_next_page_id(page.next_page_id());

    out_child_page_ids.clear();

    if (is_leaf) {
        for (std::uint16_t i = 0; i < num_keys; ++i) {
            IndexKey key;
            auto status = IndexKey::deserialize(page_bytes, offset, key);
            if (!status.ok()) {
                return status;
            }

            if (offset + sizeof(std::uint16_t) > PAGE_SIZE) {
                return common::Status::InvalidArgument("Corrupted leaf index page: EOF reading rid count");
            }

            std::uint16_t rid_count = 0;
            std::memcpy(&rid_count, page_bytes.data() + offset, sizeof(rid_count));
            offset += sizeof(rid_count);

            std::vector<storage::RecordId> rids;
            rids.reserve(rid_count);
            for (std::uint16_t j = 0; j < rid_count; ++j) {
                if (offset + sizeof(page_id_t) + sizeof(std::uint32_t) > PAGE_SIZE) {
                    return common::Status::InvalidArgument("Corrupted leaf index page: EOF reading RecordId");
                }
                storage::RecordId rid;
                std::memcpy(&rid.page_id, page_bytes.data() + offset, sizeof(rid.page_id));
                offset += sizeof(rid.page_id);
                std::memcpy(&rid.slot_id, page_bytes.data() + offset, sizeof(rid.slot_id));
                offset += sizeof(rid.slot_id);
                rids.push_back(rid);
            }

            node->keys_.push_back(std::move(key));
            node->values_.push_back(std::move(rids));
        }
    } else {
        // Internal node
        if (offset + sizeof(page_id_t) > PAGE_SIZE) {
            return common::Status::InvalidArgument("Corrupted internal index page: EOF reading child 0 PID");
        }
        page_id_t c0_pid = INVALID_PAGE_ID;
        std::memcpy(&c0_pid, page_bytes.data() + offset, sizeof(c0_pid));
        offset += sizeof(c0_pid);
        out_child_page_ids.push_back(c0_pid);

        for (std::uint16_t i = 0; i < num_keys; ++i) {
            IndexKey key;
            auto status = IndexKey::deserialize(page_bytes, offset, key);
            if (!status.ok()) {
                return status;
            }

            if (offset + sizeof(page_id_t) > PAGE_SIZE) {
                return common::Status::InvalidArgument("Corrupted internal index page: EOF reading child PID");
            }
            page_id_t c_pid = INVALID_PAGE_ID;
            std::memcpy(&c_pid, page_bytes.data() + offset, sizeof(c_pid));
            offset += sizeof(c_pid);
            out_child_page_ids.push_back(c_pid);

            node->keys_.push_back(std::move(key));
        }
    }

    out_node = std::move(node);
    return common::Status::Ok();
}

} // namespace querycraft::index
