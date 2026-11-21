#include "index/index_manager.hpp"
#include "storage/disk_manager.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <string>

using namespace querycraft;
using namespace querycraft::index;
using namespace querycraft::storage;

class IndexManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::current_path() / "data" / "test_im";
        std::error_code ec;
        std::filesystem::create_directories(test_dir_, ec);
        db_file_ = test_dir_ / "test_index_mgr.db";
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

TEST_F(IndexManagerTest, CreateAndQueryIndexes) {
    DiskManager dm(db_file_);
    ASSERT_TRUE(dm.is_open());
    IndexManager im(dm);

    ASSERT_TRUE(im.create_index("idx_students_id", "students", "id", KeyType::INT, 4).ok());
    ASSERT_TRUE(im.create_index("idx_students_gpa", "students", "gpa", KeyType::FLOAT, 4).ok());

    EXPECT_TRUE(im.has_index("idx_students_id"));
    EXPECT_TRUE(im.has_index("IDX_STUDENTS_ID")); // Case-insensitive
    EXPECT_TRUE(im.has_index("students", "id"));
    EXPECT_TRUE(im.has_index("students", "gpa"));
    EXPECT_FALSE(im.has_index("students", "name"));

    auto all_indexes = im.list_indexes();
    EXPECT_EQ(all_indexes.size(), 2);

    auto table_indexes = im.list_table_indexes("students");
    EXPECT_EQ(table_indexes.size(), 2);

    const auto* meta = im.get_metadata("idx_students_gpa");
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->key_type, KeyType::FLOAT);
    EXPECT_EQ(meta->table_name, "students");
    EXPECT_EQ(meta->column_name, "gpa");
}

TEST_F(IndexManagerTest, RejectDuplicateIndexNamesAndColumns) {
    DiskManager dm(db_file_);
    ASSERT_TRUE(dm.is_open());
    IndexManager im(dm);

    ASSERT_TRUE(im.create_index("idx_users_id", "users", "id", KeyType::INT).ok());

    // Duplicate name
    auto st_dup_name = im.create_index("idx_users_id", "users", "user_id", KeyType::INT);
    EXPECT_FALSE(st_dup_name.ok());
    EXPECT_EQ(st_dup_name.code(), common::StatusCode::AlreadyExists);

    // Duplicate column on same table
    auto st_dup_col = im.create_index("idx_another_id", "users", "id", KeyType::INT);
    EXPECT_FALSE(st_dup_col.ok());
    EXPECT_EQ(st_dup_col.code(), common::StatusCode::AlreadyExists);
}

TEST_F(IndexManagerTest, InsertSearchAndDeleteEntries) {
    DiskManager dm(db_file_);
    ASSERT_TRUE(dm.is_open());
    IndexManager im(dm);

    ASSERT_TRUE(im.create_index("idx_emp_salary", "employees", "salary", KeyType::FLOAT).ok());

    RecordId r1{1, 10}, r2{1, 20}, r3{2, 5};
    ASSERT_TRUE(im.insert_entry("idx_emp_salary", IndexKey(75000.50), r1).ok());
    ASSERT_TRUE(im.insert_entry("idx_emp_salary", IndexKey(95000.00), r2).ok());
    ASSERT_TRUE(im.insert_entry("idx_emp_salary", IndexKey(75000.50), r3).ok()); // Duplicate salary

    auto results = im.search("idx_emp_salary", IndexKey(75000.50));
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], r1);
    EXPECT_EQ(results[1], r3);

    // Delete one entry
    ASSERT_TRUE(im.delete_entry("idx_emp_salary", IndexKey(75000.50), r1).ok());
    results = im.search("idx_emp_salary", IndexKey(75000.50));
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], r3);
}

TEST_F(IndexManagerTest, KeyTypeValidation) {
    DiskManager dm(db_file_);
    ASSERT_TRUE(dm.is_open());
    IndexManager im(dm);

    ASSERT_TRUE(im.create_index("idx_code", "items", "code", KeyType::TEXT).ok());

    // Valid TEXT insertion
    EXPECT_TRUE(im.insert_entry("idx_code", IndexKey("ITEM_001"), RecordId{1, 1}).ok());

    // Incompatible INT key into TEXT index rejected
    auto status = im.insert_entry("idx_code", IndexKey(static_cast<std::int64_t>(100)), RecordId{1, 2});
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), common::StatusCode::InvalidArgument);
}

TEST_F(IndexManagerTest, DropIndex) {
    DiskManager dm(db_file_);
    ASSERT_TRUE(dm.is_open());
    IndexManager im(dm);

    ASSERT_TRUE(im.create_index("idx_temp", "t", "col", KeyType::INT).ok());
    EXPECT_TRUE(im.has_index("idx_temp"));

    ASSERT_TRUE(im.drop_index("idx_temp").ok());
    EXPECT_FALSE(im.has_index("idx_temp"));

    EXPECT_FALSE(im.drop_index("idx_temp").ok());
}

TEST_F(IndexManagerTest, PersistenceAcrossSessions) {
    page_id_t catalog_pid = INVALID_PAGE_ID;

    // Session 1: Create indexes, insert entries, persist catalog
    {
        DiskManager dm1(db_file_);
        ASSERT_TRUE(dm1.is_open());
        IndexManager im1(dm1);

        ASSERT_TRUE(im1.create_index("idx_int", "table1", "val_int", KeyType::INT, 3).ok());
        ASSERT_TRUE(im1.create_index("idx_str", "table2", "val_str", KeyType::TEXT, 3).ok());

        for (int i = 1; i <= 20; ++i) {
            ASSERT_TRUE(im1.insert_entry("idx_int", IndexKey(static_cast<std::int64_t>(i * 5)), RecordId{1, static_cast<std::uint32_t>(i)}).ok());
        }

        ASSERT_TRUE(im1.insert_entry("idx_str", IndexKey("alfa"), RecordId{2, 1}).ok());
        ASSERT_TRUE(im1.insert_entry("idx_str", IndexKey("bravo"), RecordId{2, 2}).ok());
        ASSERT_TRUE(im1.insert_entry("idx_str", IndexKey("charlie"), RecordId{2, 3}).ok());

        ASSERT_TRUE(im1.persist(catalog_pid).ok());
        EXPECT_NE(catalog_pid, INVALID_PAGE_ID);

        dm1.close();
    }

    // Session 2: Reopen from disk, load catalog, verify data
    {
        DiskManager dm2(db_file_);
        ASSERT_TRUE(dm2.is_open());
        IndexManager im2(dm2);

        ASSERT_TRUE(im2.load(catalog_pid).ok());

        EXPECT_TRUE(im2.has_index("idx_int"));
        EXPECT_TRUE(im2.has_index("idx_str"));

        // Verify integer index search
        for (int i = 1; i <= 20; ++i) {
            auto res = im2.search("idx_int", IndexKey(static_cast<std::int64_t>(i * 5)));
            ASSERT_EQ(res.size(), 1);
            EXPECT_EQ(res[0].page_id, 1);
            EXPECT_EQ(res[0].slot_id, static_cast<std::uint32_t>(i));
        }

        // Verify string index search
        auto res_str = im2.search("idx_str", IndexKey("bravo"));
        ASSERT_EQ(res_str.size(), 1);
        EXPECT_EQ(res_str[0], (RecordId{2, 2}));

        // Range query after restart
        auto range_res = im2.range_search("idx_int",
                                          IndexKey(static_cast<std::int64_t>(15)),
                                          IndexKey(static_cast<std::int64_t>(35)), true, true);
        EXPECT_EQ(range_res.size(), 5); // 15, 20, 25, 30, 35

        dm2.close();
    }
}
