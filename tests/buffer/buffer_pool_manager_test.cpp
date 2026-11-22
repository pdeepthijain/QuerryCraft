#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.hpp"
#include "common/config.hpp"
#include "index/bplus_tree.hpp"
#include "index/index_manager.hpp"
#include "storage/disk_manager.hpp"
#include "storage/record.hpp"
#include "storage/schema.hpp"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace querycraft::buffer {

class BufferPoolManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "querycraft_bpm_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "test_bpm.db";
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
};

TEST_F(BufferPoolManagerTest, NewPageAndFetchPage) {
    storage::DiskManager dm(db_path_);
    ASSERT_TRUE(dm.is_open());

    BufferPoolManager bpm(5, dm);
    EXPECT_EQ(bpm.pool_size(), 5);
    EXPECT_EQ(bpm.num_free_frames(), 5);

    page_id_t pid0 = INVALID_PAGE_ID;
    auto* page0 = bpm.NewPage(pid0);
    ASSERT_NE(page0, nullptr);
    EXPECT_EQ(pid0, 0);
    EXPECT_EQ(bpm.get_pin_count(pid0), 1);
    EXPECT_TRUE(bpm.is_cached(pid0));
    EXPECT_EQ(bpm.num_free_frames(), 4);

    // Write marker data to page
    const char* test_data = "Hello QueryCraft Buffer Pool!";
    std::memcpy(page0->data() + 100, test_data, std::strlen(test_data) + 1);

    // Unpin as dirty
    EXPECT_TRUE(bpm.UnpinPage(pid0, /*is_dirty=*/true));
    EXPECT_EQ(bpm.get_pin_count(pid0), 0);
    EXPECT_TRUE(bpm.is_dirty(pid0));
    EXPECT_EQ(bpm.num_evictable_frames(), 1);

    // Fetch page again (cache hit)
    auto* fetched = bpm.FetchPage(pid0);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(bpm.get_pin_count(pid0), 1);
    EXPECT_STREQ(reinterpret_cast<const char*>(fetched->data() + 100), test_data);
    EXPECT_EQ(bpm.num_hits(), 1);
    EXPECT_EQ(bpm.num_misses(), 0);

    EXPECT_TRUE(bpm.UnpinPage(pid0, /*is_dirty=*/false));
}

TEST_F(BufferPoolManagerTest, CacheHitAvoidsDiskRead) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(5, dm);

    page_id_t pid = INVALID_PAGE_ID;
    auto* page = bpm.NewPage(pid);
    ASSERT_NE(page, nullptr);
    bpm.UnpinPage(pid, false);

    bpm.reset_stats();

    // Fetch page while cached -> Hit, 0 disk reads
    auto* p1 = bpm.FetchPage(pid);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(bpm.num_hits(), 1);
    EXPECT_EQ(bpm.num_misses(), 0);
    EXPECT_EQ(bpm.num_reads(), 0);

    bpm.UnpinPage(pid, false);
}

TEST_F(BufferPoolManagerTest, UnpinPageDecrementsPinCount) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(3, dm);

    page_id_t pid = INVALID_PAGE_ID;
    bpm.NewPage(pid);
    EXPECT_EQ(bpm.get_pin_count(pid), 1);

    // Fetch again increases pin count to 2
    bpm.FetchPage(pid);
    EXPECT_EQ(bpm.get_pin_count(pid), 2);
    EXPECT_EQ(bpm.num_evictable_frames(), 0);

    // First unpin
    EXPECT_TRUE(bpm.UnpinPage(pid, false));
    EXPECT_EQ(bpm.get_pin_count(pid), 1);
    EXPECT_EQ(bpm.num_evictable_frames(), 0); // Still pinned, not evictable

    // Second unpin
    EXPECT_TRUE(bpm.UnpinPage(pid, false));
    EXPECT_EQ(bpm.get_pin_count(pid), 0);
    EXPECT_EQ(bpm.num_evictable_frames(), 1); // Now evictable

    // Third unpin should fail
    EXPECT_FALSE(bpm.UnpinPage(pid, false));
}

TEST_F(BufferPoolManagerTest, UnpinPageSetsDirtyFlag) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(3, dm);

    page_id_t pid = INVALID_PAGE_ID;
    bpm.NewPage(pid);
    EXPECT_FALSE(bpm.is_dirty(pid));

    // Unpin with is_dirty = true
    EXPECT_TRUE(bpm.UnpinPage(pid, true));
    EXPECT_TRUE(bpm.is_dirty(pid));

    // Fetch again and unpin with is_dirty = false: should remain dirty
    bpm.FetchPage(pid);
    EXPECT_TRUE(bpm.UnpinPage(pid, false));
    EXPECT_TRUE(bpm.is_dirty(pid));
}

TEST_F(BufferPoolManagerTest, FlushPageWritesDirtyToDisk) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(3, dm);

    page_id_t pid = INVALID_PAGE_ID;
    auto* page = bpm.NewPage(pid);
    const char* magic_str = "PERSISTED_BUFFER_DATA";
    std::memcpy(page->data() + 64, magic_str, std::strlen(magic_str) + 1);
    bpm.UnpinPage(pid, true);

    EXPECT_TRUE(bpm.is_dirty(pid));
    EXPECT_TRUE(bpm.FlushPage(pid));
    EXPECT_FALSE(bpm.is_dirty(pid));
    EXPECT_EQ(bpm.num_writes(), 1);

    // Verify disk manager has the exact bytes
    storage::Page disk_page;
    auto read_st = dm.read_page(pid, disk_page);
    ASSERT_TRUE(read_st.ok());
    EXPECT_STREQ(reinterpret_cast<const char*>(disk_page.data() + 64), magic_str);
}

TEST_F(BufferPoolManagerTest, FlushAllPagesFlushesAllDirty) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(5, dm);

    page_id_t p0 = INVALID_PAGE_ID;
    page_id_t p1 = INVALID_PAGE_ID;
    page_id_t p2 = INVALID_PAGE_ID;

    bpm.NewPage(p0);
    bpm.NewPage(p1);
    bpm.NewPage(p2);

    bpm.UnpinPage(p0, true);
    bpm.UnpinPage(p1, false); // clean
    bpm.UnpinPage(p2, true);

    EXPECT_TRUE(bpm.is_dirty(p0));
    EXPECT_FALSE(bpm.is_dirty(p1));
    EXPECT_TRUE(bpm.is_dirty(p2));

    EXPECT_TRUE(bpm.FlushAllPages());
    EXPECT_FALSE(bpm.is_dirty(p0));
    EXPECT_FALSE(bpm.is_dirty(p1));
    EXPECT_FALSE(bpm.is_dirty(p2));
    EXPECT_EQ(bpm.num_writes(), 2);
}

TEST_F(BufferPoolManagerTest, DeletePageUnpinnedReturnsFrameToFreeList) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(3, dm);

    page_id_t pid = INVALID_PAGE_ID;
    bpm.NewPage(pid);
    EXPECT_EQ(bpm.num_free_frames(), 2);

    bpm.UnpinPage(pid, false);
    EXPECT_TRUE(bpm.DeletePage(pid));
    EXPECT_FALSE(bpm.is_cached(pid));
    EXPECT_EQ(bpm.num_free_frames(), 3);
}

TEST_F(BufferPoolManagerTest, DeletePagePinnedFails) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(3, dm);

    page_id_t pid = INVALID_PAGE_ID;
    bpm.NewPage(pid); // pin_count = 1

    EXPECT_FALSE(bpm.DeletePage(pid));
    EXPECT_TRUE(bpm.is_cached(pid));

    bpm.UnpinPage(pid, false);
    EXPECT_TRUE(bpm.DeletePage(pid));
}

TEST_F(BufferPoolManagerTest, EvictionSmallPool) {
    storage::DiskManager dm(db_path_);
    // Tiny pool of 3 frames
    BufferPoolManager bpm(3, dm);

    page_id_t p0 = INVALID_PAGE_ID;
    page_id_t p1 = INVALID_PAGE_ID;
    page_id_t p2 = INVALID_PAGE_ID;

    bpm.NewPage(p0); // frame 0
    bpm.NewPage(p1); // frame 1
    bpm.NewPage(p2); // frame 2

    // Unpin in order: p0, p1, p2
    bpm.UnpinPage(p0, false);
    bpm.UnpinPage(p1, false);
    bpm.UnpinPage(p2, false);

    EXPECT_TRUE(bpm.is_cached(p0));
    EXPECT_TRUE(bpm.is_cached(p1));
    EXPECT_TRUE(bpm.is_cached(p2));
    EXPECT_EQ(bpm.num_free_frames(), 0);

    // Allocating p3 must evict the LRU page (p0)
    page_id_t p3 = INVALID_PAGE_ID;
    auto* page3 = bpm.NewPage(p3);
    ASSERT_NE(page3, nullptr);

    EXPECT_FALSE(bpm.is_cached(p0)); // Evicted!
    EXPECT_TRUE(bpm.is_cached(p1));
    EXPECT_TRUE(bpm.is_cached(p2));
    EXPECT_TRUE(bpm.is_cached(p3));

    bpm.UnpinPage(p3, false);
}

TEST_F(BufferPoolManagerTest, DirtyPageEvictionFlushesBeforeReuse) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(2, dm);

    page_id_t p0 = INVALID_PAGE_ID;
    auto* page0 = bpm.NewPage(p0);
    const char* marker0 = "DIRTY_PAGE_0_DATA";
    std::memcpy(page0->data() + 200, marker0, std::strlen(marker0) + 1);
    bpm.UnpinPage(p0, /*is_dirty=*/true);

    page_id_t p1 = INVALID_PAGE_ID;
    auto* page1 = bpm.NewPage(p1);
    const char* marker1 = "PAGE_1_DATA";
    std::memcpy(page1->data() + 200, marker1, std::strlen(marker1) + 1);
    bpm.UnpinPage(p1, /*is_dirty=*/false);

    // Allocating p2 will evict p0 (the LRU page).
    // Because p0 is dirty, BPM must flush it before overwriting its frame!
    page_id_t p2 = INVALID_PAGE_ID;
    auto* page2 = bpm.NewPage(p2);
    ASSERT_NE(page2, nullptr);
    bpm.UnpinPage(p2, false);

    EXPECT_FALSE(bpm.is_cached(p0));
    EXPECT_GE(bpm.num_writes(), 1);

    // Now re-fetch p0 from disk: should have our data intact!
    auto* refetched_p0 = bpm.FetchPage(p0);
    ASSERT_NE(refetched_p0, nullptr);
    EXPECT_STREQ(reinterpret_cast<const char*>(refetched_p0->data() + 200), marker0);
    bpm.UnpinPage(p0, false);
}

TEST_F(BufferPoolManagerTest, CleanPageEvictionDoesNotWrite) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(2, dm);

    page_id_t p0 = INVALID_PAGE_ID;
    bpm.NewPage(p0);
    bpm.UnpinPage(p0, /*is_dirty=*/false);

    page_id_t p1 = INVALID_PAGE_ID;
    bpm.NewPage(p1);
    bpm.UnpinPage(p1, /*is_dirty=*/false);

    bpm.reset_stats();

    // Evict p0 by allocating p2
    page_id_t p2 = INVALID_PAGE_ID;
    auto* page2 = bpm.NewPage(p2);
    ASSERT_NE(page2, nullptr);
    bpm.UnpinPage(p2, false);

    // Eviction of clean page should not trigger any disk write
    EXPECT_EQ(bpm.num_writes(), 0);
}

TEST_F(BufferPoolManagerTest, AllPinnedPreventsAllocation) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(2, dm);

    page_id_t p0 = INVALID_PAGE_ID;
    page_id_t p1 = INVALID_PAGE_ID;
    bpm.NewPage(p0); // pinned (pin_count=1)
    bpm.NewPage(p1); // pinned (pin_count=1)

    // Cannot allocate or fetch non-cached page when all frames are pinned
    page_id_t p2 = INVALID_PAGE_ID;
    EXPECT_EQ(bpm.NewPage(p2), nullptr);
    EXPECT_EQ(p2, INVALID_PAGE_ID);

    EXPECT_EQ(bpm.FetchPage(999), nullptr);

    // Unpin one page, now allocation succeeds
    bpm.UnpinPage(p0, false);
    auto* page2 = bpm.NewPage(p2);
    ASSERT_NE(page2, nullptr);
    EXPECT_EQ(p2, 2);

    bpm.UnpinPage(p1, false);
    bpm.UnpinPage(p2, false);
}

TEST_F(BufferPoolManagerTest, PinnedPageNeverEvicted) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(2, dm);

    page_id_t p0 = INVALID_PAGE_ID;
    bpm.NewPage(p0); // p0 pinned

    page_id_t p1 = INVALID_PAGE_ID;
    bpm.NewPage(p1);
    bpm.UnpinPage(p1, false); // p1 evictable

    // Allocate p2: p0 is older, but it's PINNED, so p1 MUST be evicted instead!
    page_id_t p2 = INVALID_PAGE_ID;
    auto* page2 = bpm.NewPage(p2);
    ASSERT_NE(page2, nullptr);

    EXPECT_TRUE(bpm.is_cached(p0));
    EXPECT_FALSE(bpm.is_cached(p1));
    EXPECT_TRUE(bpm.is_cached(p2));

    bpm.UnpinPage(p0, false);
    bpm.UnpinPage(p2, false);
}

TEST_F(BufferPoolManagerTest, LRUEvictionOrderRespected) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(3, dm);

    page_id_t p0 = INVALID_PAGE_ID;
    page_id_t p1 = INVALID_PAGE_ID;
    page_id_t p2 = INVALID_PAGE_ID;

    bpm.NewPage(p0);
    bpm.NewPage(p1);
    bpm.NewPage(p2);

    bpm.UnpinPage(p0, false);
    bpm.UnpinPage(p1, false);
    bpm.UnpinPage(p2, false);

    // Access p0: fetch and unpin it, making p0 MRU!
    bpm.FetchPage(p0);
    bpm.UnpinPage(p0, false);

    // LRU order of evictable frames is now: p1, p2, p0.
    // Next allocation must evict p1!
    page_id_t p3 = INVALID_PAGE_ID;
    bpm.NewPage(p3);

    EXPECT_TRUE(bpm.is_cached(p0));
    EXPECT_FALSE(bpm.is_cached(p1)); // Evicted!
    EXPECT_TRUE(bpm.is_cached(p2));
    EXPECT_TRUE(bpm.is_cached(p3));

    bpm.UnpinPage(p3, false);
}

TEST_F(BufferPoolManagerTest, RestartRecovery) {
    {
        storage::DiskManager dm(db_path_);
        BufferPoolManager bpm(4, dm);

        for (int i = 0; i < 5; ++i) {
            page_id_t pid = INVALID_PAGE_ID;
            auto* p = bpm.NewPage(pid);
            ASSERT_NE(p, nullptr);
            const std::string text = "PersistentRecord_" + std::to_string(i);
            std::memcpy(p->data() + 100, text.data(), text.size() + 1);
            bpm.UnpinPage(pid, true);
        }

        EXPECT_TRUE(bpm.FlushAllPages());
    } // BPM and DiskManager destroyed and flushed

    // Reopen database with fresh DiskManager and fresh BPM
    {
        storage::DiskManager dm(db_path_);
        ASSERT_TRUE(dm.is_open());
        EXPECT_EQ(dm.num_pages(), 5);

        BufferPoolManager bpm(3, dm); // Smaller pool than num_pages

        for (int i = 0; i < 5; ++i) {
            auto* p = bpm.FetchPage(static_cast<page_id_t>(i));
            ASSERT_NE(p, nullptr);
            const std::string expected = "PersistentRecord_" + std::to_string(i);
            EXPECT_STREQ(reinterpret_cast<const char*>(p->data() + 100), expected.c_str());
            bpm.UnpinPage(static_cast<page_id_t>(i), false);
        }
    }
}

TEST_F(BufferPoolManagerTest, BPlusTreeIntegrationWithBPM) {
    page_id_t root_pid = INVALID_PAGE_ID;

    {
        storage::DiskManager dm(db_path_);
        // Tiny 3-frame buffer pool to test heavy page eviction during tree persistence
        BufferPoolManager bpm(3, dm);

        index::BPlusTree tree(/*max_keys=*/3);

        // Insert 25 keys to create a multi-level tree
        for (int i = 1; i <= 25; ++i) {
            auto st = tree.insert(index::IndexKey(i), storage::RecordId{1, static_cast<std::uint32_t>(i)});
            ASSERT_TRUE(st.ok());
        }

        EXPECT_TRUE(tree.validate_tree().ok());

        // Persist tree through BufferPoolManager
        auto persist_st = tree.persist(bpm, root_pid);
        ASSERT_TRUE(persist_st.ok());
        EXPECT_NE(root_pid, INVALID_PAGE_ID);
    }

    // Reopen and load B+ Tree through fresh BPM
    {
        storage::DiskManager dm(db_path_);
        BufferPoolManager bpm(3, dm);

        std::unique_ptr<index::BPlusTree> loaded_tree;
        auto load_st = index::BPlusTree::load(bpm, root_pid, /*max_keys=*/3, loaded_tree);
        ASSERT_TRUE(load_st.ok());
        ASSERT_NE(loaded_tree, nullptr);

        EXPECT_TRUE(loaded_tree->validate_tree().ok());

        // Search for all keys
        for (int i = 1; i <= 25; ++i) {
            auto rids = loaded_tree->search(index::IndexKey(i));
            ASSERT_EQ(rids.size(), 1);
            EXPECT_EQ(rids[0].page_id, 1);
            EXPECT_EQ(rids[0].slot_id, static_cast<std::uint32_t>(i));
        }

        // Range search
        auto range_res = loaded_tree->range_search(index::IndexKey(10), index::IndexKey(15));
        EXPECT_EQ(range_res.size(), 6);
    }
}

TEST_F(BufferPoolManagerTest, IndexManagerIntegrationWithBPM) {
    page_id_t catalog_pid = INVALID_PAGE_ID;

    {
        storage::DiskManager dm(db_path_);
        BufferPoolManager bpm(4, dm);

        index::IndexManager im(dm);
        ASSERT_TRUE(im.create_index("idx_student_id", "students", "id", index::KeyType::INT, 4).ok());
        ASSERT_TRUE(im.create_index("idx_student_name", "students", "name", index::KeyType::TEXT, 4).ok());

        // Insert entries
        for (int i = 10; i <= 30; i += 5) {
            ASSERT_TRUE(im.insert_entry("idx_student_id", index::IndexKey(i), storage::RecordId{2, static_cast<std::uint32_t>(i)}).ok());
        }

        ASSERT_TRUE(im.insert_entry("idx_student_name", index::IndexKey(std::string("Alice")), storage::RecordId{2, 1}).ok());
        ASSERT_TRUE(im.insert_entry("idx_student_name", index::IndexKey(std::string("Bob")), storage::RecordId{2, 2}).ok());

        // Persist index catalog and all trees through BPM
        ASSERT_TRUE(im.persist(bpm, catalog_pid).ok());
        EXPECT_NE(catalog_pid, INVALID_PAGE_ID);
    }

    // Reopen and reload via fresh BPM
    {
        storage::DiskManager dm(db_path_);
        BufferPoolManager bpm(4, dm);

        index::IndexManager im(dm);
        ASSERT_TRUE(im.load(bpm, catalog_pid).ok());

        EXPECT_TRUE(im.has_index("idx_student_id"));
        EXPECT_TRUE(im.has_index("idx_student_name"));

        auto id_rids = im.search("idx_student_id", index::IndexKey(20));
        ASSERT_EQ(id_rids.size(), 1);
        EXPECT_EQ(id_rids[0].slot_id, 20);

        auto name_rids = im.search("idx_student_name", index::IndexKey(std::string("Alice")));
        ASSERT_EQ(name_rids.size(), 1);
        EXPECT_EQ(name_rids[0].slot_id, 1);
    }
}

TEST_F(BufferPoolManagerTest, SlottedPageStorageThroughBPM) {
    storage::DiskManager dm(db_path_);
    BufferPoolManager bpm(3, dm);

    storage::Schema schema({
        storage::Column("id", storage::DataType::INT),
        storage::Column("name", storage::DataType::VARCHAR, 32),
        storage::Column("score", storage::DataType::FLOAT)
    });

    page_id_t pid = INVALID_PAGE_ID;
    auto* page = bpm.NewPage(pid);
    ASSERT_NE(page, nullptr);

    storage::Record r1(std::vector<storage::Value>{
        storage::Value(101),
        storage::Value(std::string("Alice")),
        storage::Value(95.5)
    });

    storage::Record r2(std::vector<storage::Value>{
        storage::Value(102),
        storage::Value(std::string("Bob")),
        storage::Value(88.0)
    });

    ASSERT_TRUE(r1.validate(schema).ok());
    ASSERT_TRUE(r2.validate(schema).ok());

    auto r1_bytes = r1.serialize();
    auto r2_bytes = r2.serialize();

    std::uint32_t slot1 = 0;
    std::uint32_t slot2 = 0;
    ASSERT_TRUE(page->insert_record(r1_bytes, slot1).ok());
    ASSERT_TRUE(page->insert_record(r2_bytes, slot2).ok());

    bpm.UnpinPage(pid, /*is_dirty=*/true);
    EXPECT_TRUE(bpm.FlushPage(pid));

    // Refetch page and verify slotted-page contents
    auto* fetched = bpm.FetchPage(pid);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->num_slots(), 2);

    std::vector<std::uint8_t> read_bytes;
    ASSERT_TRUE(fetched->get_record(slot1, read_bytes).ok());

    storage::Record rec_out;
    ASSERT_TRUE(storage::Record::deserialize(read_bytes, rec_out).ok());
    EXPECT_EQ(rec_out.get_value(0).as_int(), 101);
    EXPECT_EQ(rec_out.get_value(1).as_string(), "Alice");
    EXPECT_DOUBLE_EQ(rec_out.get_value(2).as_float(), 95.5);

    bpm.UnpinPage(pid, false);
}

} // namespace querycraft::buffer
