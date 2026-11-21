#include "index/bplus_tree.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <stdexcept>

namespace querycraft::index {

BPlusTree::BPlusTree(std::size_t max_keys) : max_keys_(std::max<std::size_t>(3, max_keys)) {}

std::shared_ptr<BPlusTreeNode> BPlusTree::find_leaf_node(const IndexKey& key) const {
    if (!root_) {
        return nullptr;
    }
    auto curr = root_;
    while (!curr->is_leaf()) {
        const auto child_idx = curr->find_child_index_for_key(key);
        curr = curr->children()[child_idx];
    }
    return curr;
}

std::shared_ptr<BPlusTreeNode> BPlusTree::find_leftmost_leaf() const {
    if (!root_) {
        return nullptr;
    }
    auto curr = root_;
    while (!curr->is_leaf()) {
        curr = curr->children().front();
    }
    return curr;
}

common::Status BPlusTree::insert(const IndexKey& key, storage::RecordId rid) {
    if (!root_) {
        root_ = std::make_shared<BPlusTreeNode>(/*is_leaf=*/true, max_keys_);
        root_->insert_leaf_record(key, rid);
        return common::Status::Ok();
    }

    auto leaf = find_leaf_node(key);
    leaf->insert_leaf_record(key, rid);

    if (leaf->is_overflow()) {
        auto new_leaf = std::make_shared<BPlusTreeNode>(/*is_leaf=*/true, max_keys_);
        const auto separator = leaf->split_leaf(new_leaf);
        insert_into_parent(leaf, separator, new_leaf);
    }

    return common::Status::Ok();
}

void BPlusTree::insert_into_parent(std::shared_ptr<BPlusTreeNode> left,
                                  const IndexKey& separator,
                                  std::shared_ptr<BPlusTreeNode> right) {
    if (left == root_) {
        auto new_root = std::make_shared<BPlusTreeNode>(/*is_leaf=*/false, max_keys_);
        new_root->keys().push_back(separator);
        new_root->children().push_back(left);
        new_root->children().push_back(right);
        left->set_parent(new_root);
        right->set_parent(new_root);
        root_ = new_root;
        return;
    }

    auto parent = left->parent();
    const auto& children = parent->children();
    auto it = std::find(children.begin(), children.end(), left);
    if (it == children.end()) {
        throw std::logic_error("Left child not found in parent during insert_into_parent");
    }
    const auto child_idx = static_cast<std::size_t>(std::distance(children.begin(), it));

    const auto c_idx = static_cast<std::ptrdiff_t>(child_idx);
    parent->keys().insert(parent->keys().begin() + c_idx, separator);
    parent->children().insert(parent->children().begin() + c_idx + 1, right);
    right->set_parent(parent);

    if (parent->is_overflow()) {
        auto new_parent = std::make_shared<BPlusTreeNode>(/*is_leaf=*/false, max_keys_);
        IndexKey pushed_separator;
        parent->split_internal(new_parent, pushed_separator);
        insert_into_parent(parent, pushed_separator, new_parent);
    }
}

std::vector<storage::RecordId> BPlusTree::search(const IndexKey& key) const {
    if (!root_) {
        return {};
    }
    const auto leaf = find_leaf_node(key);
    const auto idx = leaf->find_key_index(key);
    if (idx < leaf->key_count()) {
        return leaf->values()[idx];
    }
    return {};
}

common::Status BPlusTree::delete_entry(const IndexKey& key, storage::RecordId rid) {
    if (!root_) {
        return common::Status::NotFound("B+ Tree is empty");
    }

    auto leaf = find_leaf_node(key);
    if (!leaf->delete_leaf_record(key, rid)) {
        return common::Status::NotFound("Key or RecordId not found in B+ Tree");
    }

    if (leaf->is_underflow(leaf == root_)) {
        handle_underflow(leaf);
    }

    return common::Status::Ok();
}

void BPlusTree::handle_underflow(std::shared_ptr<BPlusTreeNode> node) {
    if (node == root_) {
        if (!node->is_leaf() && node->keys().empty()) {
            if (node->children().size() == 1) {
                root_ = node->children()[0];
                root_->set_parent(nullptr);
            } else if (node->children().empty()) {
                root_ = nullptr;
            }
        } else if (node->is_leaf() && node->keys().empty()) {
            root_ = nullptr;
        }
        return;
    }

    auto parent = node->parent();
    const auto& children = parent->children();
    auto it = std::find(children.begin(), children.end(), node);
    if (it == children.end()) {
        throw std::logic_error("Node not found in parent's children during underflow");
    }
    const auto child_idx = static_cast<std::size_t>(std::distance(children.begin(), it));

    std::shared_ptr<BPlusTreeNode> left_sibling = (child_idx > 0) ? children[child_idx - 1] : nullptr;
    std::shared_ptr<BPlusTreeNode> right_sibling = (child_idx + 1 < children.size()) ? children[child_idx + 1] : nullptr;

    // Strategy 1: Borrow from left sibling
    if (left_sibling && left_sibling->keys().size() > left_sibling->min_keys()) {
        if (node->is_leaf()) {
            const auto key = left_sibling->keys().back();
            const auto rids = left_sibling->values().back();
            left_sibling->keys().pop_back();
            left_sibling->values().pop_back();

            node->keys().insert(node->keys().begin(), key);
            node->values().insert(node->values().begin(), rids);

            parent->keys()[child_idx - 1] = node->keys()[0];
        } else {
            const auto parent_sep = parent->keys()[child_idx - 1];
            const auto stolen_key = left_sibling->keys().back();
            const auto stolen_child = left_sibling->children().back();
            left_sibling->keys().pop_back();
            left_sibling->children().pop_back();

            node->keys().insert(node->keys().begin(), parent_sep);
            node->children().insert(node->children().begin(), stolen_child);
            stolen_child->set_parent(node);

            parent->keys()[child_idx - 1] = stolen_key;
        }
        return;
    }

    // Strategy 2: Borrow from right sibling
    if (right_sibling && right_sibling->keys().size() > right_sibling->min_keys()) {
        if (node->is_leaf()) {
            const auto key = right_sibling->keys().front();
            const auto rids = right_sibling->values().front();
            right_sibling->keys().erase(right_sibling->keys().begin());
            right_sibling->values().erase(right_sibling->values().begin());

            node->keys().push_back(key);
            node->values().push_back(rids);

            parent->keys()[child_idx] = right_sibling->keys()[0];
        } else {
            const auto parent_sep = parent->keys()[child_idx];
            const auto stolen_key = right_sibling->keys().front();
            const auto stolen_child = right_sibling->children().front();
            right_sibling->keys().erase(right_sibling->keys().begin());
            right_sibling->children().erase(right_sibling->children().begin());

            node->keys().push_back(parent_sep);
            node->children().push_back(stolen_child);
            stolen_child->set_parent(node);

            parent->keys()[child_idx] = stolen_key;
        }
        return;
    }

    // Strategy 3: Merge with sibling
    if (left_sibling) {
        // Merge node into left_sibling
        if (node->is_leaf()) {
            left_sibling->keys().insert(left_sibling->keys().end(), node->keys().begin(), node->keys().end());
            left_sibling->values().insert(left_sibling->values().end(), node->values().begin(), node->values().end());

            left_sibling->set_next_leaf(node->next_leaf());
            left_sibling->set_next_page_id(node->next_page_id());
            if (node->next_leaf()) {
                node->next_leaf()->set_prev_leaf(left_sibling);
                node->next_leaf()->set_prev_page_id(left_sibling->page_id());
            }

            const auto c_idx = static_cast<std::ptrdiff_t>(child_idx);
            parent->keys().erase(parent->keys().begin() + (c_idx - 1));
            parent->children().erase(parent->children().begin() + c_idx);
        } else {
            const auto sep = parent->keys()[child_idx - 1];
            left_sibling->keys().push_back(sep);
            left_sibling->keys().insert(left_sibling->keys().end(), node->keys().begin(), node->keys().end());
            left_sibling->children().insert(left_sibling->children().end(), node->children().begin(), node->children().end());

            for (const auto& child : node->children()) {
                child->set_parent(left_sibling);
            }

            const auto c_idx = static_cast<std::ptrdiff_t>(child_idx);
            parent->keys().erase(parent->keys().begin() + (c_idx - 1));
            parent->children().erase(parent->children().begin() + c_idx);
        }

        if (parent->is_underflow(parent == root_)) {
            handle_underflow(parent);
        }
    } else if (right_sibling) {
        // Merge right_sibling into node
        if (node->is_leaf()) {
            node->keys().insert(node->keys().end(), right_sibling->keys().begin(), right_sibling->keys().end());
            node->values().insert(node->values().end(), right_sibling->values().begin(), right_sibling->values().end());

            node->set_next_leaf(right_sibling->next_leaf());
            node->set_next_page_id(right_sibling->next_page_id());
            if (right_sibling->next_leaf()) {
                right_sibling->next_leaf()->set_prev_leaf(node);
                right_sibling->next_leaf()->set_prev_page_id(node->page_id());
            }

            const auto c_idx = static_cast<std::ptrdiff_t>(child_idx);
            parent->keys().erase(parent->keys().begin() + c_idx);
            parent->children().erase(parent->children().begin() + c_idx + 1);
        } else {
            const auto sep = parent->keys()[child_idx];
            node->keys().push_back(sep);
            node->keys().insert(node->keys().end(), right_sibling->keys().begin(), right_sibling->keys().end());
            node->children().insert(node->children().end(), right_sibling->children().begin(), right_sibling->children().end());

            for (const auto& child : right_sibling->children()) {
                child->set_parent(node);
            }

            const auto c_idx = static_cast<std::ptrdiff_t>(child_idx);
            parent->keys().erase(parent->keys().begin() + c_idx);
            parent->children().erase(parent->children().begin() + c_idx + 1);
        }

        if (parent->is_underflow(parent == root_)) {
            handle_underflow(parent);
        }
    }
}

std::vector<std::pair<IndexKey, std::vector<storage::RecordId>>> BPlusTree::range_search(
    const IndexKey& lower,
    const IndexKey& upper,
    bool lower_inclusive,
    bool upper_inclusive) const {
    if (!root_) {
        return {};
    }

    std::vector<std::pair<IndexKey, std::vector<storage::RecordId>>> results;
    auto curr = find_leaf_node(lower);

    while (curr) {
        for (std::size_t i = 0; i < curr->key_count(); ++i) {
            const auto& k = curr->keys()[i];

            if (lower_inclusive ? (k < lower) : (k <= lower)) {
                continue;
            }

            if (upper_inclusive ? (k > upper) : (k >= upper)) {
                return results;
            }

            results.emplace_back(k, curr->values()[i]);
        }
        curr = curr->next_leaf();
    }

    return results;
}

std::vector<storage::RecordId> BPlusTree::range_search_records(
    const IndexKey& lower,
    const IndexKey& upper,
    bool lower_inclusive,
    bool upper_inclusive) const {
    const auto pairs = range_search(lower, upper, lower_inclusive, upper_inclusive);
    std::vector<storage::RecordId> rids;
    for (const auto& [_, list] : pairs) {
        rids.insert(rids.end(), list.begin(), list.end());
    }
    return rids;
}

std::vector<std::pair<IndexKey, std::vector<storage::RecordId>>> BPlusTree::ordered_traversal() const {
    if (!root_) {
        return {};
    }

    std::vector<std::pair<IndexKey, std::vector<storage::RecordId>>> results;
    auto curr = find_leftmost_leaf();

    while (curr) {
        for (std::size_t i = 0; i < curr->key_count(); ++i) {
            results.emplace_back(curr->keys()[i], curr->values()[i]);
        }
        curr = curr->next_leaf();
    }

    return results;
}

common::Status BPlusTree::validate_tree() const {
    if (!root_) {
        return common::Status::Ok(); // Empty tree is valid
    }

    if (root_->parent() != nullptr) {
        return common::Status::InternalError("Validation failed: root parent is not null");
    }

    int leaf_depth = -1;

    auto check_node = [&](auto& self, const std::shared_ptr<BPlusTreeNode>& node, int depth) -> common::Status {
        if (!node) {
            return common::Status::InternalError("Validation failed: null node encountered");
        }

        // 1. Sorted keys check
        for (std::size_t i = 0; i + 1 < node->key_count(); ++i) {
            if (!(node->keys()[i] < node->keys()[i + 1])) {
                return common::Status::InternalError("Validation failed: keys are not strictly sorted in node");
            }
        }

        // 2. Capacity bounds check
        if (node->keys().size() > node->max_keys()) {
            return common::Status::InternalError("Validation failed: node key count exceeds max_keys");
        }

        if (node != root_ && node->keys().size() < node->min_keys()) {
            return common::Status::InternalError("Validation failed: non-root node underflowed below min_keys");
        }

        // 3. Leaf specific checks
        if (node->is_leaf()) {
            if (node->values().size() != node->keys().size()) {
                return common::Status::InternalError("Validation failed: leaf values size != keys size");
            }

            for (const auto& rids : node->values()) {
                if (rids.empty()) {
                    return common::Status::InternalError("Validation failed: empty RecordId list in leaf entry");
                }
            }

            if (leaf_depth == -1) {
                leaf_depth = depth;
            } else if (leaf_depth != depth) {
                return common::Status::InternalError("Validation failed: leaves exist at different depths");
            }

            if (node->next_leaf()) {
                if (node->next_leaf()->prev_leaf() != node) {
                    return common::Status::InternalError("Validation failed: broken doubly-linked leaf links");
                }
            }
            return common::Status::Ok();
        }

        // 4. Internal specific checks
        if (node->children().size() != node->key_count() + 1) {
            return common::Status::InternalError("Validation failed: internal children count != keys count + 1");
        }

        auto get_min_key = [](auto& get_min, const std::shared_ptr<BPlusTreeNode>& n) -> IndexKey {
            if (n->is_leaf()) {
                return n->keys().front();
            }
            return get_min(get_min, n->children().front());
        };

        auto get_max_key = [](auto& get_max, const std::shared_ptr<BPlusTreeNode>& n) -> IndexKey {
            if (n->is_leaf()) {
                return n->keys().back();
            }
            return get_max(get_max, n->children().back());
        };

        for (std::size_t i = 0; i < node->children().size(); ++i) {
            const auto& child = node->children()[i];
            if (child->parent() != node) {
                return common::Status::InternalError("Validation failed: child parent pointer does not point to node");
            }

            // Separator checks
            if (i > 0) {
                const auto& sep = node->keys()[i - 1];
                if (child->key_count() > 0) {
                    const auto min_k = get_min_key(get_min_key, child);
                    if (min_k < sep) {
                        return common::Status::InternalError("Validation failed: child key violates separator invariant");
                    }
                }
            }
            if (i < node->key_count()) {
                const auto& sep = node->keys()[i];
                if (child->key_count() > 0) {
                    const auto max_k = get_max_key(get_max_key, child);
                    if (!(max_k < sep)) {
                        return common::Status::InternalError("Validation failed: child key violates upper separator invariant");
                    }
                }
            }

            auto st = self(self, child, depth + 1);
            if (!st.ok()) return st;
        }

        return common::Status::Ok();
    };

    return check_node(check_node, root_, 0);
}

common::Status BPlusTree::persist(storage::DiskManager& dm, page_id_t& out_root_pid) {
    if (!root_) {
        out_root_pid = INVALID_PAGE_ID;
        return common::Status::Ok();
    }

    // Allocate page IDs for any nodes that do not yet have a valid page_id
    std::vector<std::shared_ptr<BPlusTreeNode>> all_nodes;
    std::queue<std::shared_ptr<BPlusTreeNode>> q;
    q.push(root_);

    while (!q.empty()) {
        auto node = q.front();
        q.pop();
        all_nodes.push_back(node);

        if (node->page_id() == INVALID_PAGE_ID) {
            page_id_t new_pid = INVALID_PAGE_ID;
            auto alloc_status = dm.allocate_page(new_pid);
            if (!alloc_status.ok()) {
                return alloc_status;
            }
            node->set_page_id(new_pid);
        }

        if (!node->is_leaf()) {
            for (const auto& child : node->children()) {
                q.push(child);
            }
        }
    }

    // Update leaf page IDs
    for (const auto& node : all_nodes) {
        if (node->is_leaf()) {
            node->set_next_page_id(node->next_leaf() ? node->next_leaf()->page_id() : INVALID_PAGE_ID);
            node->set_prev_page_id(node->prev_leaf() ? node->prev_leaf()->page_id() : INVALID_PAGE_ID);
        }
    }

    // Serialize each node into its 4KB disk page
    for (const auto& node : all_nodes) {
        storage::Page p;
        auto ser_status = node->serialize_to_page(p);
        if (!ser_status.ok()) {
            return ser_status;
        }
        auto write_status = dm.write_page(node->page_id(), p);
        if (!write_status.ok()) {
            return write_status;
        }
    }

    auto flush_status = dm.flush();
    if (!flush_status.ok()) {
        return flush_status;
    }

    out_root_pid = root_->page_id();
    return common::Status::Ok();
}

common::Status BPlusTree::load(storage::DiskManager& dm,
                              page_id_t root_pid,
                              std::size_t max_keys,
                              std::unique_ptr<BPlusTree>& out_tree) {
    if (root_pid == INVALID_PAGE_ID) {
        out_tree = std::make_unique<BPlusTree>(max_keys);
        return common::Status::Ok();
    }

    struct NodeInfo {
        std::shared_ptr<BPlusTreeNode> node;
        std::vector<page_id_t> child_pids;
        page_id_t parent_pid;
    };

    std::map<page_id_t, NodeInfo> loaded;
    std::queue<page_id_t> to_load;
    to_load.push(root_pid);

    while (!to_load.empty()) {
        const auto pid = to_load.front();
        to_load.pop();

        if (loaded.find(pid) != loaded.end()) {
            continue;
        }

        storage::Page page;
        auto read_status = dm.read_page(pid, page);
        if (!read_status.ok()) {
            return read_status;
        }

        std::shared_ptr<BPlusTreeNode> node;
        std::vector<page_id_t> child_pids;
        page_id_t parent_pid = INVALID_PAGE_ID;

        auto des_status = BPlusTreeNode::deserialize_from_page(page, max_keys, node, child_pids, parent_pid);
        if (!des_status.ok()) {
            return des_status;
        }

        loaded[pid] = NodeInfo{node, child_pids, parent_pid};

        for (const auto cpid : child_pids) {
            if (cpid != INVALID_PAGE_ID) {
                to_load.push(cpid);
            }
        }
    }

    // Connect child pointers and parent pointers
    for (auto& [pid, info] : loaded) {
        if (!info.node->is_leaf()) {
            for (const auto cpid : info.child_pids) {
                auto it = loaded.find(cpid);
                if (it == loaded.end()) {
                    return common::Status::InternalError("Corrupted index: referenced child page " + std::to_string(cpid) + " not found");
                }
                info.node->children().push_back(it->second.node);
                it->second.node->set_parent(info.node);
            }
        }
    }

    // Reconnect leaf links
    for (auto& [pid, info] : loaded) {
        if (info.node->is_leaf()) {
            if (info.node->next_page_id() != INVALID_PAGE_ID) {
                auto it = loaded.find(info.node->next_page_id());
                if (it != loaded.end()) {
                    info.node->set_next_leaf(it->second.node);
                    it->second.node->set_prev_leaf(info.node);
                }
            }
        }
    }

    auto tree = std::make_unique<BPlusTree>(max_keys);
    auto root_it = loaded.find(root_pid);
    if (root_it == loaded.end()) {
        return common::Status::InternalError("Failed to find root node in loaded index pages");
    }
    tree->root_ = root_it->second.node;

    out_tree = std::move(tree);
    return common::Status::Ok();
}

} // namespace querycraft::index
