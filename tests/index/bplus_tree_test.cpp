#include "index/bplus_tree.hpp"
#include "index/index_key.hpp"
#include "storage/disk_manager.hpp"

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace querycraft;
using namespace querycraft::index;
using namespace querycraft::storage;

TEST(BPlusTreeTest, BasicOperations) {
    BPlusTree tree(3);
    EXPECT_TRUE(tree.is_empty());

    // Insert one key
    IndexKey k1(static_cast<std::int64_t>(42));
    RecordId r1{1, 10};
    EXPECT_TRUE(tree.insert(k1, r1).ok());
    EXPECT_FALSE(tree.is_empty());

    // Search existing
    auto found = tree.search(k1);
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found[0], r1);

    // Search missing
    IndexKey k_missing(static_cast<std::int64_t>(999));
    auto not_found = tree.search(k_missing);
    EXPECT_TRUE(not_found.empty());

    // Insert multiple keys and check ordered traversal
    IndexKey k2(static_cast<std::int64_t>(10));
    RecordId r2{1, 5};
    IndexKey k3(static_cast<std::int64_t>(80));
    RecordId r3{2, 1};

    EXPECT_TRUE(tree.insert(k2, r2).ok());
    EXPECT_TRUE(tree.insert(k3, r3).ok());

    auto traversal = tree.ordered_traversal();
    ASSERT_EQ(traversal.size(), 3);
    EXPECT_EQ(traversal[0].first.as_int(), 10);
    EXPECT_EQ(traversal[1].first.as_int(), 42);
    EXPECT_EQ(traversal[2].first.as_int(), 80);

    EXPECT_TRUE(tree.validate_tree().ok());
}

TEST(BPlusTreeTest, SplittingLeafAndInternalNodes) {
    // Degree = 3: max 3 keys per node. 4th key triggers split!
    BPlusTree tree(3);

    std::vector<std::int64_t> keys = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        ASSERT_TRUE(tree.insert(IndexKey(keys[i]), RecordId{1, static_cast<std::uint32_t>(i)}).ok());
        auto st = tree.validate_tree();
        ASSERT_TRUE(st.ok()) << st.to_string() << " at step i=" << i << " (inserted key " << keys[i] << ")";
    }

    // Verify all keys can be searched
    for (std::size_t i = 0; i < keys.size(); ++i) {
        auto res = tree.search(IndexKey(keys[i]));
        ASSERT_EQ(res.size(), 1);
        EXPECT_EQ(res[0].slot_id, static_cast<std::uint32_t>(i));
    }

    auto traversal = tree.ordered_traversal();
    ASSERT_EQ(traversal.size(), keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        EXPECT_EQ(traversal[i].first.as_int(), keys[i]);
    }
}

TEST(BPlusTreeTest, RepeatedInsertionsSortedAndReverseOrder) {
    // Sorted order insertions
    {
        BPlusTree tree_asc(3);
        for (int i = 1; i <= 50; ++i) {
            ASSERT_TRUE(tree_asc.insert(IndexKey(static_cast<std::int64_t>(i)), RecordId{1, static_cast<std::uint32_t>(i)}).ok());
        }
        EXPECT_TRUE(tree_asc.validate_tree().ok());
        auto trav = tree_asc.ordered_traversal();
        ASSERT_EQ(trav.size(), 50);
        for (std::size_t i = 0; i < trav.size(); ++i) {
            EXPECT_EQ(trav[i].first.as_int(), static_cast<std::int64_t>(i + 1));
        }
    }

    // Reverse order insertions
    {
        BPlusTree tree_desc(3);
        for (int i = 50; i >= 1; --i) {
            ASSERT_TRUE(tree_desc.insert(IndexKey(static_cast<std::int64_t>(i)), RecordId{1, static_cast<std::uint32_t>(i)}).ok());
        }
        EXPECT_TRUE(tree_desc.validate_tree().ok());
        auto trav = tree_desc.ordered_traversal();
        ASSERT_EQ(trav.size(), 50);
        for (std::size_t i = 0; i < trav.size(); ++i) {
            EXPECT_EQ(trav[i].first.as_int(), static_cast<std::int64_t>(i + 1));
        }
    }
}

TEST(BPlusTreeTest, DuplicateKeyHandling) {
    BPlusTree tree(3);

    IndexKey dup_key(static_cast<std::int64_t>(100));
    RecordId r1{1, 1}, r2{1, 2}, r3{2, 5};

    ASSERT_TRUE(tree.insert(dup_key, r1).ok());
    ASSERT_TRUE(tree.insert(dup_key, r2).ok());
    ASSERT_TRUE(tree.insert(dup_key, r3).ok());

    auto results = tree.search(dup_key);
    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0], r1);
    EXPECT_EQ(results[1], r2);
    EXPECT_EQ(results[2], r3);

    // Delete one duplicate
    ASSERT_TRUE(tree.delete_entry(dup_key, r2).ok());
    results = tree.search(dup_key);
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], r1);
    EXPECT_EQ(results[1], r3);

    // Delete remaining duplicates
    ASSERT_TRUE(tree.delete_entry(dup_key, r1).ok());
    ASSERT_TRUE(tree.delete_entry(dup_key, r3).ok());
    EXPECT_TRUE(tree.search(dup_key).empty());
    EXPECT_TRUE(tree.is_empty());
    EXPECT_TRUE(tree.validate_tree().ok());
}

TEST(BPlusTreeTest, DeletionRedistributionAndMerging) {
    BPlusTree tree(3);

    std::vector<int> nums = {10, 20, 30, 40, 50, 60, 70, 80};
    for (int n : nums) {
        ASSERT_TRUE(tree.insert(IndexKey(static_cast<std::int64_t>(n)), RecordId{1, static_cast<std::uint32_t>(n)}).ok());
    }
    ASSERT_TRUE(tree.validate_tree().ok());

    // Delete non-existent key
    EXPECT_FALSE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(999)), RecordId{1, 1}).ok());

    // Delete middle key triggering leaf redistribution or merging
    ASSERT_TRUE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(40)), RecordId{1, 40}).ok());
    EXPECT_TRUE(tree.validate_tree().ok());
    EXPECT_TRUE(tree.search(IndexKey(static_cast<std::int64_t>(40))).empty());

    // Delete multiple keys to trigger internal node merging & root shrinking
    ASSERT_TRUE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(50)), RecordId{1, 50}).ok());
    ASSERT_TRUE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(60)), RecordId{1, 60}).ok());
    ASSERT_TRUE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(70)), RecordId{1, 70}).ok());
    ASSERT_TRUE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(80)), RecordId{1, 80}).ok());
    EXPECT_TRUE(tree.validate_tree().ok());

    // Remaining keys: 10, 20, 30
    auto trav = tree.ordered_traversal();
    ASSERT_EQ(trav.size(), 3);
    EXPECT_EQ(trav[0].first.as_int(), 10);
    EXPECT_EQ(trav[1].first.as_int(), 20);
    EXPECT_EQ(trav[2].first.as_int(), 30);

    // Delete all remaining keys
    ASSERT_TRUE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(10)), RecordId{1, 10}).ok());
    ASSERT_TRUE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(20)), RecordId{1, 20}).ok());
    ASSERT_TRUE(tree.delete_entry(IndexKey(static_cast<std::int64_t>(30)), RecordId{1, 30}).ok());
    EXPECT_TRUE(tree.is_empty());
    EXPECT_TRUE(tree.validate_tree().ok());
}

TEST(BPlusTreeTest, RangeQueries) {
    BPlusTree tree(3);

    for (int i = 10; i <= 100; i += 10) {
        ASSERT_TRUE(tree.insert(IndexKey(static_cast<std::int64_t>(i)), RecordId{1, static_cast<std::uint32_t>(i)}).ok());
    }

    // Inclusive range: [30, 70]
    auto res_inc = tree.range_search(IndexKey(static_cast<std::int64_t>(30)),
                                     IndexKey(static_cast<std::int64_t>(70)), true, true);
    ASSERT_EQ(res_inc.size(), 5);
    EXPECT_EQ(res_inc.front().first.as_int(), 30);
    EXPECT_EQ(res_inc.back().first.as_int(), 70);

    // Exclusive range: (30, 70)
    auto res_exc = tree.range_search(IndexKey(static_cast<std::int64_t>(30)),
                                     IndexKey(static_cast<std::int64_t>(70)), false, false);
    ASSERT_EQ(res_exc.size(), 3);
    EXPECT_EQ(res_exc.front().first.as_int(), 40);
    EXPECT_EQ(res_exc.back().first.as_int(), 60);

    // Empty range: (50, 50)
    auto res_empty = tree.range_search(IndexKey(static_cast<std::int64_t>(50)),
                                       IndexKey(static_cast<std::int64_t>(50)), false, false);
    EXPECT_TRUE(res_empty.empty());

    // Full range: [0, 200]
    auto res_full = tree.range_search(IndexKey(static_cast<std::int64_t>(0)),
                                      IndexKey(static_cast<std::int64_t>(200)), true, true);
    EXPECT_EQ(res_full.size(), 10);
}

TEST(BPlusTreeTest, DataTypesSupport) {
    // Floating point keys
    {
        BPlusTree f_tree(3);
        std::vector<double> floats = {3.14, 1.41, 2.71, 0.57, 9.81, 1.61};
        for (std::size_t i = 0; i < floats.size(); ++i) {
            ASSERT_TRUE(f_tree.insert(IndexKey(floats[i]), RecordId{1, static_cast<std::uint32_t>(i)}).ok());
        }
        EXPECT_TRUE(f_tree.validate_tree().ok());
        auto trav = f_tree.ordered_traversal();
        std::sort(floats.begin(), floats.end());
        ASSERT_EQ(trav.size(), floats.size());
        for (std::size_t i = 0; i < floats.size(); ++i) {
            EXPECT_DOUBLE_EQ(trav[i].first.as_float(), floats[i]);
        }
    }

    // String / Text keys
    {
        BPlusTree s_tree(3);
        std::vector<std::string> words = {"zebra", "apple", "mango", "banana", "cherry", "orange"};
        for (std::size_t i = 0; i < words.size(); ++i) {
            ASSERT_TRUE(s_tree.insert(IndexKey(words[i]), RecordId{1, static_cast<std::uint32_t>(i)}).ok());
        }
        EXPECT_TRUE(s_tree.validate_tree().ok());
        auto trav = s_tree.ordered_traversal();
        std::sort(words.begin(), words.end());
        ASSERT_EQ(trav.size(), words.size());
        for (std::size_t i = 0; i < words.size(); ++i) {
            EXPECT_EQ(trav[i].first.as_text(), words[i]);
        }
    }

    // Incompatible type comparisons
    IndexKey int_key(static_cast<std::int64_t>(10));
    IndexKey str_key("ten");
    EXPECT_THROW((void)(int_key == str_key), std::invalid_argument);

    // NULL key handling
    IndexKey null_key;
    EXPECT_TRUE(null_key.is_null());
    EXPECT_TRUE(null_key < int_key); // NULLS FIRST
    EXPECT_TRUE(null_key == IndexKey());
}

TEST(BPlusTreeTest, RandomizedStressTestAgainstReferenceModel) {
    // Reference model using std::map strictly for validation
    std::map<std::int64_t, std::vector<RecordId>> ref_map;
    BPlusTree tree(4);

    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<std::int64_t> key_dist(1, 100);
    std::uniform_int_distribution<int> op_dist(0, 9); // 0..6 insert, 7..9 delete

    for (int step = 0; step < 400; ++step) {
        const auto key_val = key_dist(rng);
        IndexKey key(key_val);
        const int op = op_dist(rng);

        if (op < 7) {
            // Insert
            RecordId rid{static_cast<page_id_t>(step / 10), static_cast<std::uint32_t>(step % 10)};
            auto& ref_vec = ref_map[key_val];
            if (std::find(ref_vec.begin(), ref_vec.end(), rid) == ref_vec.end()) {
                ref_vec.push_back(rid);
            }
            ASSERT_TRUE(tree.insert(key, rid).ok());
        } else {
            // Delete
            auto it = ref_map.find(key_val);
            if (it != ref_map.end() && !it->second.empty()) {
                RecordId rid = it->second.back();
                it->second.pop_back();
                if (it->second.empty()) {
                    ref_map.erase(it);
                }
                ASSERT_TRUE(tree.delete_entry(key, rid).ok());
            }
        }

        if (step % 50 == 0) {
            ASSERT_TRUE(tree.validate_tree().ok());
        }
    }

    ASSERT_TRUE(tree.validate_tree().ok());

    // Compare tree traversal against ref_map
    auto traversal = tree.ordered_traversal();
    ASSERT_EQ(traversal.size(), ref_map.size());

    auto ref_it = ref_map.begin();
    for (std::size_t i = 0; i < traversal.size(); ++i, ++ref_it) {
        EXPECT_EQ(traversal[i].first.as_int(), ref_it->first);
        EXPECT_EQ(traversal[i].second.size(), ref_it->second.size());
    }
}

TEST(BPlusTreeTest, PersistenceAndRestartRecovery) {
    const auto test_dir = std::filesystem::current_path() / "data" / "test_bpt_persist";
    std::error_code ec;
    std::filesystem::create_directories(test_dir, ec);
    const auto db_file = test_dir / "index_test.db";
    std::filesystem::remove(db_file, ec);

    page_id_t root_pid = INVALID_PAGE_ID;

    // Session 1: Build tree, persist to disk, close
    {
        DiskManager dm(db_file);
        ASSERT_TRUE(dm.is_open());

        BPlusTree tree(3);
        for (int i = 1; i <= 30; ++i) {
            ASSERT_TRUE(tree.insert(IndexKey(static_cast<std::int64_t>(i * 10)),
                                   RecordId{1, static_cast<std::uint32_t>(i)}).ok());
        }

        ASSERT_TRUE(tree.validate_tree().ok());
        ASSERT_TRUE(tree.persist(dm, root_pid).ok());
        EXPECT_NE(root_pid, INVALID_PAGE_ID);

        dm.close();
    }

    // Session 2: Reopen disk manager, load tree from root_pid
    {
        DiskManager dm(db_file);
        ASSERT_TRUE(dm.is_open());

        std::unique_ptr<BPlusTree> loaded_tree;
        ASSERT_TRUE(BPlusTree::load(dm, root_pid, 3, loaded_tree).ok());
        ASSERT_NE(loaded_tree, nullptr);

        EXPECT_TRUE(loaded_tree->validate_tree().ok());

        // Verify all 30 keys can be searched accurately
        for (int i = 1; i <= 30; ++i) {
            auto res = loaded_tree->search(IndexKey(static_cast<std::int64_t>(i * 10)));
            ASSERT_EQ(res.size(), 1);
            EXPECT_EQ(res[0].slot_id, static_cast<std::uint32_t>(i));
        }

        // Verify leaf links with ordered traversal
        auto trav = loaded_tree->ordered_traversal();
        ASSERT_EQ(trav.size(), 30);
        for (std::size_t i = 0; i < 30; ++i) {
            EXPECT_EQ(trav[i].first.as_int(), static_cast<std::int64_t>((i + 1) * 10));
        }

        dm.close();
    }

    std::filesystem::remove(db_file, ec);
    std::filesystem::remove_all(test_dir, ec);
}
