#include "storage/database.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace querycraft;
using namespace querycraft::storage;

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::current_path() / "data" / "test_db";
        std::error_code ec;
        std::filesystem::create_directories(test_dir_, ec);
        db_file_ = test_dir_ / "test_querycraft.db";
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

TEST_F(DatabaseTest, CreateTablesAndList) {
    Database db;
    ASSERT_TRUE(db.open(db_file_).ok());

    Schema user_schema({
        Column("id", DataType::INT, true, false),
        Column("username", DataType::VARCHAR, false, false, 32),
        Column("score", DataType::FLOAT, false, true)
    });

    Schema course_schema({
        Column("course_code", DataType::VARCHAR, true, false, 10),
        Column("credits", DataType::INT, false, false)
    });

    ASSERT_TRUE(db.create_table("users", user_schema).ok());
    ASSERT_TRUE(db.create_table("courses", course_schema).ok());

    EXPECT_TRUE(db.has_table("users"));
    EXPECT_TRUE(db.has_table("USERS")); // Case-insensitive lookup
    EXPECT_TRUE(db.has_table("courses"));
    EXPECT_FALSE(db.has_table("non_existent"));

    auto tables = db.list_tables();
    EXPECT_EQ(tables.size(), 2);
}

TEST_F(DatabaseTest, RejectDuplicateTableNames) {
    Database db(db_file_);
    ASSERT_TRUE(db.is_open());

    Schema schema({
        Column("id", DataType::INT, true, false)
    });

    ASSERT_TRUE(db.create_table("students", schema).ok());

    // Exact duplicate
    auto status = db.create_table("students", schema);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), common::StatusCode::AlreadyExists);

    // Case-insensitive duplicate
    status = db.create_table("STUDENTS", schema);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), common::StatusCode::AlreadyExists);
}

TEST_F(DatabaseTest, InsertAndRetrieveRecords) {
    Database db(db_file_);
    ASSERT_TRUE(db.is_open());

    Schema schema({
        Column("id", DataType::INT, true, false),
        Column("name", DataType::VARCHAR, false, false, 50),
        Column("cgpa", DataType::FLOAT, false, true)
    });

    ASSERT_TRUE(db.create_table("students", schema).ok());

    Record r1({Value(static_cast<std::int64_t>(101)), Value("Alice"), Value(9.8)});
    Record r2({Value(static_cast<std::int64_t>(102)), Value("Bob"), Value(8.5)});

    RecordId rid1, rid2;
    ASSERT_TRUE(db.insert_record("students", r1, rid1).ok());
    ASSERT_TRUE(db.insert_record("students", r2, rid2).ok());

    EXPECT_TRUE(rid1.is_valid());
    EXPECT_TRUE(rid2.is_valid());

    Record out1, out2;
    ASSERT_TRUE(db.get_record("students", rid1, out1).ok());
    ASSERT_TRUE(db.get_record("students", rid2, out2).ok());

    EXPECT_EQ(out1.get_value(0).as_int(), 101);
    EXPECT_EQ(out1.get_value(1).as_string(), "Alice");
    EXPECT_DOUBLE_EQ(out1.get_value(2).as_float(), 9.8);

    EXPECT_EQ(out2.get_value(0).as_int(), 102);
    EXPECT_EQ(out2.get_value(1).as_string(), "Bob");
    EXPECT_DOUBLE_EQ(out2.get_value(2).as_float(), 8.5);
}

TEST_F(DatabaseTest, UpdateRecords) {
    Database db(db_file_);
    ASSERT_TRUE(db.is_open());

    Schema schema({
        Column("id", DataType::INT, true, false),
        Column("name", DataType::VARCHAR, false, false, 50),
        Column("cgpa", DataType::FLOAT, false, true)
    });

    ASSERT_TRUE(db.create_table("students", schema).ok());

    Record r1({Value(static_cast<std::int64_t>(1)), Value("Original Name"), Value(7.0)});
    RecordId rid;
    ASSERT_TRUE(db.insert_record("students", r1, rid).ok());

    // Update with new data
    Record updated({Value(static_cast<std::int64_t>(1)), Value("Updated Name"), Value(9.9)});
    ASSERT_TRUE(db.update_record("students", rid, updated).ok());

    Record retrieved;
    ASSERT_TRUE(db.get_record("students", rid, retrieved).ok());
    EXPECT_EQ(retrieved.get_value(1).as_string(), "Updated Name");
    EXPECT_DOUBLE_EQ(retrieved.get_value(2).as_float(), 9.9);
}

TEST_F(DatabaseTest, DeleteRecords) {
    Database db(db_file_);
    ASSERT_TRUE(db.is_open());

    Schema schema({
        Column("id", DataType::INT, true, false),
        Column("name", DataType::VARCHAR, false, false, 50)
    });

    ASSERT_TRUE(db.create_table("students", schema).ok());

    Record r1({Value(static_cast<std::int64_t>(1)), Value("Alice")});
    Record r2({Value(static_cast<std::int64_t>(2)), Value("Bob")});

    RecordId rid1, rid2;
    ASSERT_TRUE(db.insert_record("students", r1, rid1).ok());
    ASSERT_TRUE(db.insert_record("students", r2, rid2).ok());

    // Delete Alice
    ASSERT_TRUE(db.delete_record("students", rid1).ok());

    Record out;
    EXPECT_FALSE(db.get_record("students", rid1, out).ok());
    EXPECT_TRUE(db.get_record("students", rid2, out).ok());

    std::vector<Record> scan_res;
    ASSERT_TRUE(db.scan_table("students", scan_res).ok());
    EXPECT_EQ(scan_res.size(), 1);
    EXPECT_EQ(scan_res[0].get_value(1).as_string(), "Bob");
}

TEST_F(DatabaseTest, FullTableScanAcrossMultiplePages) {
    Database db(db_file_);
    ASSERT_TRUE(db.is_open());

    Schema schema({
        Column("id", DataType::INT, true, false),
        Column("description", DataType::TEXT, false, false)
    });

    ASSERT_TRUE(db.create_table("items", schema).ok());

    // Insert 60 large records (~300 bytes each) which will span several 4KB pages
    const std::string text(250, 'X');
    constexpr int COUNT = 60;

    for (int i = 0; i < COUNT; ++i) {
        Record r({Value(static_cast<std::int64_t>(i)), Value("Item_" + std::to_string(i) + "_" + text)});
        RecordId rid;
        ASSERT_TRUE(db.insert_record("items", r, rid).ok());
    }

    std::vector<Record> records;
    ASSERT_TRUE(db.scan_table("items", records).ok());
    EXPECT_EQ(records.size(), static_cast<std::size_t>(COUNT));

    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(records[i].get_value(0).as_int(), static_cast<std::int64_t>(i));
    }
}

TEST_F(DatabaseTest, RecoverTableSchemasAfterRestart) {
    // Session 1: Create tables and close
    {
        Database db1(db_file_);
        ASSERT_TRUE(db1.is_open());

        Schema s1({
            Column("emp_id", DataType::INT, true, false),
            Column("first_name", DataType::VARCHAR, false, false, 40),
            Column("salary", DataType::FLOAT, false, true)
        });

        Schema s2({
            Column("dept_id", DataType::INT, true, false),
            Column("dept_name", DataType::VARCHAR, false, false, 60),
            Column("notes", DataType::TEXT, false, true)
        });

        ASSERT_TRUE(db1.create_table("employees", s1).ok());
        ASSERT_TRUE(db1.create_table("departments", s2).ok());
        db1.close();
    }

    // Session 2: Reopen from disk and verify schemas
    {
        Database db2(db_file_);
        ASSERT_TRUE(db2.is_open());

        EXPECT_TRUE(db2.has_table("employees"));
        EXPECT_TRUE(db2.has_table("departments"));

        const Table* emp_tbl = db2.get_table("employees");
        ASSERT_NE(emp_tbl, nullptr);
        const Schema& emp_schema = emp_tbl->schema();
        EXPECT_EQ(emp_schema.column_count(), 3);
        EXPECT_EQ(emp_schema.get_column(0).name(), "emp_id");
        EXPECT_TRUE(emp_schema.get_column(0).is_primary_key());
        EXPECT_EQ(emp_schema.get_column(1).name(), "first_name");
        EXPECT_EQ(emp_schema.get_column(1).length(), 40);
        EXPECT_EQ(emp_schema.get_column(2).type(), DataType::FLOAT);

        const Table* dept_tbl = db2.get_table("departments");
        ASSERT_NE(dept_tbl, nullptr);
        const Schema& dept_schema = dept_tbl->schema();
        EXPECT_EQ(dept_schema.column_count(), 3);
        EXPECT_EQ(dept_schema.get_column(1).name(), "dept_name");
        EXPECT_EQ(dept_schema.get_column(2).type(), DataType::TEXT);
    }
}

TEST_F(DatabaseTest, RecoverRecordsAfterRestart) {
    // Session 1: Create table, insert records, close database
    {
        Database db1(db_file_);
        ASSERT_TRUE(db1.is_open());

        Schema schema({
            Column("id", DataType::INT, true, false),
            Column("title", DataType::VARCHAR, false, false, 100),
            Column("rating", DataType::FLOAT, false, true)
        });

        ASSERT_TRUE(db1.create_table("movies", schema).ok());

        Record r1({Value(static_cast<std::int64_t>(1)), Value("Inception"), Value(8.8)});
        Record r2({Value(static_cast<std::int64_t>(2)), Value("Interstellar"), Value(8.7)});
        Record r3({Value(static_cast<std::int64_t>(3)), Value("The Dark Knight"), Value(9.0)});

        RecordId rid;
        ASSERT_TRUE(db1.insert_record("movies", r1, rid).ok());
        ASSERT_TRUE(db1.insert_record("movies", r2, rid).ok());
        ASSERT_TRUE(db1.insert_record("movies", r3, rid).ok());

        db1.close();
    }

    // Session 2: Reopen database and verify persisted records
    {
        Database db2(db_file_);
        ASSERT_TRUE(db2.is_open());
        ASSERT_TRUE(db2.has_table("movies"));

        std::vector<Record> records;
        ASSERT_TRUE(db2.scan_table("movies", records).ok());
        ASSERT_EQ(records.size(), 3);

        EXPECT_EQ(records[0].get_value(0).as_int(), 1);
        EXPECT_EQ(records[0].get_value(1).as_string(), "Inception");
        EXPECT_DOUBLE_EQ(records[0].get_value(2).as_float(), 8.8);

        EXPECT_EQ(records[1].get_value(0).as_int(), 2);
        EXPECT_EQ(records[1].get_value(1).as_string(), "Interstellar");
        EXPECT_DOUBLE_EQ(records[1].get_value(2).as_float(), 8.7);

        EXPECT_EQ(records[2].get_value(0).as_int(), 3);
        EXPECT_EQ(records[2].get_value(1).as_string(), "The Dark Knight");
        EXPECT_DOUBLE_EQ(records[2].get_value(2).as_float(), 9.0);

        // Can insert additional record into recovered table
        Record r4({Value(static_cast<std::int64_t>(4)), Value("Memento"), Value(8.4)});
        RecordId rid4;
        ASSERT_TRUE(db2.insert_record("movies", r4, rid4).ok());

        records.clear();
        ASSERT_TRUE(db2.scan_table("movies", records).ok());
        EXPECT_EQ(records.size(), 4);
    }
}
