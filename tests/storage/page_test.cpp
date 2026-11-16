#include "storage/page.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace querycraft;
using namespace querycraft::storage;

TEST(PageTest, PageSizeVerification) {
    EXPECT_EQ(Page::size(), 4096);
    Page page;
    EXPECT_GE(sizeof(page), 4096);
    EXPECT_EQ(page.page_id(), INVALID_PAGE_ID);
    EXPECT_EQ(page.num_slots(), 0);
    EXPECT_EQ(page.free_space_pointer(), 4096);
    EXPECT_EQ(page.free_space_remaining(), 4096 - PAGE_HEADER_SIZE);
}

TEST(PageTest, HeaderPropertySettersAndGetters) {
    Page page(42, PageType::TABLE_DATA_PAGE);
    EXPECT_EQ(page.page_id(), 42);
    EXPECT_EQ(page.page_type(), PageType::TABLE_DATA_PAGE);

    page.set_prev_page_id(10);
    page.set_next_page_id(20);
    EXPECT_EQ(page.prev_page_id(), 10);
    EXPECT_EQ(page.next_page_id(), 20);

    page.reset(99, PageType::CATALOG_PAGE);
    EXPECT_EQ(page.page_id(), 99);
    EXPECT_EQ(page.page_type(), PageType::CATALOG_PAGE);
    EXPECT_EQ(page.prev_page_id(), INVALID_PAGE_ID);
    EXPECT_EQ(page.next_page_id(), INVALID_PAGE_ID);
    EXPECT_EQ(page.num_slots(), 0);
}

TEST(PageTest, PageBoundaryChecks) {
    Page page;
    std::vector<std::uint8_t> src(100, 0xAB);
    
    // Normal write within bounds
    auto status = page.write_bytes(100, src);
    EXPECT_TRUE(status.ok());

    std::vector<std::uint8_t> read_dest(100);
    status = page.read_bytes(100, read_dest);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(src, read_dest);

    // Out-of-bounds write: offset 4050 + 100 > 4096
    status = page.write_bytes(4050, src);
    EXPECT_FALSE(status.ok());

    // Out-of-bounds read: offset 4050 + 100 > 4096
    status = page.read_bytes(4050, read_dest);
    EXPECT_FALSE(status.ok());

    // Edge of page write: offset 3996 + 100 = 4096 (exact match)
    status = page.write_bytes(PAGE_SIZE - 100, src);
    EXPECT_TRUE(status.ok());
}

TEST(PageTest, SlottedPageInsertAndRetrieveOneRecord) {
    Page page(1, PageType::TABLE_DATA_PAGE);
    const std::string payload = "Hello QueryCraft Relational Engine";
    std::span<const std::uint8_t> bytes(
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());

    std::uint32_t slot_id = 999;
    auto status = page.insert_record(bytes, slot_id);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(slot_id, 0);
    EXPECT_EQ(page.num_slots(), 1);

    std::vector<std::uint8_t> out_data;
    status = page.get_record(0, out_data);
    EXPECT_TRUE(status.ok());
    std::string retrieved(out_data.begin(), out_data.end());
    EXPECT_EQ(retrieved, payload);
}

TEST(PageTest, SlottedPageInsertMultipleRecords) {
    Page page(1, PageType::TABLE_DATA_PAGE);
    std::vector<std::string> messages = {
        "Record 0: Systems Programming",
        "Record 1: B+ Tree Indexing",
        "Record 2: Buffer Pool Management",
        "Record 3: Query Processing and Optimization"
    };

    std::vector<std::uint32_t> slot_ids;
    for (const auto& msg : messages) {
        std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
        std::uint32_t slot = 0;
        ASSERT_TRUE(page.insert_record(bytes, slot).ok());
        slot_ids.push_back(slot);
    }

    EXPECT_EQ(page.num_slots(), 4);

    for (std::size_t i = 0; i < messages.size(); ++i) {
        std::vector<std::uint8_t> out_data;
        ASSERT_TRUE(page.get_record(slot_ids[i], out_data).ok());
        std::string retrieved(out_data.begin(), out_data.end());
        EXPECT_EQ(retrieved, messages[i]);
    }
}

TEST(PageTest, SlottedPageDeleteAndSlotReuse) {
    Page page(1, PageType::TABLE_DATA_PAGE);
    const std::string rec0 = "First record";
    const std::string rec1 = "Second record to be deleted";
    const std::string rec2 = "Third record";

    std::uint32_t s0 = 0, s1 = 0, s2 = 0;
    ASSERT_TRUE(page.insert_record({reinterpret_cast<const std::uint8_t*>(rec0.data()), rec0.size()}, s0).ok());
    ASSERT_TRUE(page.insert_record({reinterpret_cast<const std::uint8_t*>(rec1.data()), rec1.size()}, s1).ok());
    ASSERT_TRUE(page.insert_record({reinterpret_cast<const std::uint8_t*>(rec2.data()), rec2.size()}, s2).ok());
    EXPECT_EQ(s0, 0);
    EXPECT_EQ(s1, 1);
    EXPECT_EQ(s2, 2);

    // Delete record 1
    ASSERT_TRUE(page.delete_record(s1).ok());
    EXPECT_TRUE(page.is_slot_deleted(s1));

    std::vector<std::uint8_t> out_data;
    EXPECT_FALSE(page.get_record(s1, out_data).ok());

    // Insert a new record; should reuse slot 1!
    const std::string new_rec = "Replaced second record";
    std::uint32_t s_new = 0;
    ASSERT_TRUE(page.insert_record({reinterpret_cast<const std::uint8_t*>(new_rec.data()), new_rec.size()}, s_new).ok());
    EXPECT_EQ(s_new, 1); // Reused slot 1!

    ASSERT_TRUE(page.get_record(1, out_data).ok());
    std::string retrieved(out_data.begin(), out_data.end());
    EXPECT_EQ(retrieved, new_rec);
}

TEST(PageTest, SlottedPageCompaction) {
    Page page(1, PageType::TABLE_DATA_PAGE);
    std::vector<std::string> recs = {"Alfa", "Bravo", "Charlie", "Delta", "Echo"};

    for (const auto& r : recs) {
        std::uint32_t s = 0;
        ASSERT_TRUE(page.insert_record({reinterpret_cast<const std::uint8_t*>(r.data()), r.size()}, s).ok());
    }

    const auto free_before_delete = page.free_space_remaining();
    // Delete Bravo (1) and Delta (3)
    ASSERT_TRUE(page.delete_record(1).ok());
    ASSERT_TRUE(page.delete_record(3).ok());

    // Compacting page
    page.compact();

    // Verify Alfa (0), Charlie (2), and Echo (4) are still intact
    std::vector<std::uint8_t> out;
    ASSERT_TRUE(page.get_record(0, out).ok());
    EXPECT_EQ(std::string(out.begin(), out.end()), "Alfa");

    ASSERT_TRUE(page.get_record(2, out).ok());
    EXPECT_EQ(std::string(out.begin(), out.end()), "Charlie");

    ASSERT_TRUE(page.get_record(4, out).ok());
    EXPECT_EQ(std::string(out.begin(), out.end()), "Echo");

    // Free space should have expanded from compacting deleted space
    EXPECT_GT(page.free_space_remaining(), free_before_delete);
}

TEST(PageTest, SlottedPageFullCondition) {
    Page page(1, PageType::TABLE_DATA_PAGE);
    // Fill page with 256-byte records
    std::vector<std::uint8_t> chunk(256, 0x77);

    std::uint32_t inserted = 0;
    while (true) {
        std::uint32_t slot = 0;
        auto status = page.insert_record(chunk, slot);
        if (!status.ok()) {
            break;
        }
        ++inserted;
    }

    // Page must hold around ~15 chunks of 256 bytes plus slot overhead
    EXPECT_GE(inserted, 14);
    EXPECT_LE(inserted, 16);

    // Any further insertion must fail gracefully without crash or corruption
    std::uint32_t slot = 0;
    auto fail_status = page.insert_record(chunk, slot);
    EXPECT_FALSE(fail_status.ok());
}

TEST(PageTest, SlottedPageUpdateRecord) {
    Page page(1, PageType::TABLE_DATA_PAGE);
    const std::string initial = "Initial payload 12345";
    std::uint32_t slot = 0;
    ASSERT_TRUE(page.insert_record({reinterpret_cast<const std::uint8_t*>(initial.data()), initial.size()}, slot).ok());

    // Smaller update in-place
    const std::string smaller = "Smaller";
    ASSERT_TRUE(page.update_record(slot, {reinterpret_cast<const std::uint8_t*>(smaller.data()), smaller.size()}).ok());

    std::vector<std::uint8_t> out;
    ASSERT_TRUE(page.get_record(slot, out).ok());
    EXPECT_EQ(std::string(out.begin(), out.end()), smaller);

    // Larger update
    const std::string larger = "Much longer payload that requires moving and compacting on page";
    ASSERT_TRUE(page.update_record(slot, {reinterpret_cast<const std::uint8_t*>(larger.data()), larger.size()}).ok());
    ASSERT_TRUE(page.get_record(slot, out).ok());
    EXPECT_EQ(std::string(out.begin(), out.end()), larger);
}
