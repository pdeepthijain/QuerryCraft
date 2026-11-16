#include "storage/disk_manager.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <string>

using namespace querycraft;
using namespace querycraft::storage;

class DiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::current_path() / "data" / "test_dm";
        std::error_code ec;
        std::filesystem::create_directories(test_dir_, ec);
        db_file_ = test_dir_ / "test_storage.db";
        std::filesystem::remove(db_file_, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(db_file_, ec);
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_file_;
};

TEST_F(DiskManagerTest, CreateDatabaseFile) {
    DiskManager dm;
    EXPECT_FALSE(dm.is_open());

    auto status = dm.open(db_file_);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(dm.is_open());
    EXPECT_TRUE(std::filesystem::exists(db_file_));
    EXPECT_EQ(dm.num_pages(), 0);

    dm.close();
    EXPECT_FALSE(dm.is_open());
}

TEST_F(DiskManagerTest, AllocatePagesAndVerifyFileSize) {
    DiskManager dm(db_file_);
    ASSERT_TRUE(dm.is_open());

    page_id_t p0 = -1, p1 = -1, p2 = -1;
    ASSERT_TRUE(dm.allocate_page(p0).ok());
    ASSERT_TRUE(dm.allocate_page(p1).ok());
    ASSERT_TRUE(dm.allocate_page(p2).ok());

    EXPECT_EQ(p0, 0);
    EXPECT_EQ(p1, 1);
    EXPECT_EQ(p2, 2);
    EXPECT_EQ(dm.num_pages(), 3);

    dm.close();

    const auto file_size = std::filesystem::file_size(db_file_);
    EXPECT_EQ(file_size, 3 * PAGE_SIZE);
}

TEST_F(DiskManagerTest, WriteAndReadPages) {
    DiskManager dm(db_file_);
    ASSERT_TRUE(dm.is_open());

    page_id_t p0 = -1, p1 = -1;
    ASSERT_TRUE(dm.allocate_page(p0).ok());
    ASSERT_TRUE(dm.allocate_page(p1).ok());

    Page page0(p0, PageType::CATALOG_PAGE);
    std::vector<std::uint8_t> data0(128, 0xAA);
    ASSERT_TRUE(page0.write_bytes(100, data0).ok());
    ASSERT_TRUE(dm.write_page(p0, page0).ok());

    Page page1(p1, PageType::TABLE_DATA_PAGE);
    std::vector<std::uint8_t> data1(128, 0xBB);
    ASSERT_TRUE(page1.write_bytes(200, data1).ok());
    ASSERT_TRUE(dm.write_page(p1, page1).ok());

    Page read0, read1;
    ASSERT_TRUE(dm.read_page(p0, read0).ok());
    ASSERT_TRUE(dm.read_page(p1, read1).ok());

    EXPECT_EQ(read0.page_id(), p0);
    EXPECT_EQ(read0.page_type(), PageType::CATALOG_PAGE);
    std::vector<std::uint8_t> check0(128);
    ASSERT_TRUE(read0.read_bytes(100, check0).ok());
    EXPECT_EQ(check0, data0);

    EXPECT_EQ(read1.page_id(), p1);
    EXPECT_EQ(read1.page_type(), PageType::TABLE_DATA_PAGE);
    std::vector<std::uint8_t> check1(128);
    ASSERT_TRUE(read1.read_bytes(200, check1).ok());
    EXPECT_EQ(check1, data1);
}

TEST_F(DiskManagerTest, PersistenceAcrossInstancesAndReopen) {
    // Instance 1: write 2 pages
    {
        DiskManager dm1(db_file_);
        ASSERT_TRUE(dm1.is_open());

        page_id_t p0 = -1, p1 = -1;
        ASSERT_TRUE(dm1.allocate_page(p0).ok());
        ASSERT_TRUE(dm1.allocate_page(p1).ok());

        Page page(p1, PageType::TABLE_DATA_PAGE);
        const std::string text = "Persistent storage across restarts in QueryCraft";
        ASSERT_TRUE(page.write_bytes(64, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}).ok());
        ASSERT_TRUE(dm1.write_page(p1, page).ok());
        dm1.close();
    }

    // Instance 2: reopen and verify
    {
        DiskManager dm2(db_file_);
        ASSERT_TRUE(dm2.is_open());
        EXPECT_EQ(dm2.num_pages(), 2);

        Page page;
        ASSERT_TRUE(dm2.read_page(1, page).ok());
        EXPECT_EQ(page.page_id(), 1);

        const std::string expected = "Persistent storage across restarts in QueryCraft";
        std::vector<std::uint8_t> buf(expected.size());
        ASSERT_TRUE(page.read_bytes(64, buf).ok());
        EXPECT_EQ(std::string(buf.begin(), buf.end()), expected);

        // Allocate a 3rd page
        page_id_t p2 = -1;
        ASSERT_TRUE(dm2.allocate_page(p2).ok());
        EXPECT_EQ(p2, 2);
        EXPECT_EQ(dm2.num_pages(), 3);
    }

    // Instance 3: verify total 3 pages
    {
        DiskManager dm3(db_file_);
        EXPECT_EQ(dm3.num_pages(), 3);
    }
}

TEST_F(DiskManagerTest, InvalidPageReadsAndBoundsChecks) {
    DiskManager dm(db_file_);
    ASSERT_TRUE(dm.is_open());

    page_id_t p0 = -1;
    ASSERT_TRUE(dm.allocate_page(p0).ok());

    Page dummy;
    // Negative page id
    EXPECT_FALSE(dm.read_page(-1, dummy).ok());

    // Out of bounds page id
    EXPECT_FALSE(dm.read_page(5, dummy).ok());

    // Write out of bounds
    EXPECT_FALSE(dm.write_page(5, dummy).ok());
}

TEST_F(DiskManagerTest, OperationsOnClosedFile) {
    DiskManager dm;
    Page dummy;
    page_id_t pid = -1;

    EXPECT_FALSE(dm.allocate_page(pid).ok());
    EXPECT_FALSE(dm.read_page(0, dummy).ok());
    EXPECT_FALSE(dm.write_page(0, dummy).ok());
    EXPECT_FALSE(dm.flush().ok());
}
