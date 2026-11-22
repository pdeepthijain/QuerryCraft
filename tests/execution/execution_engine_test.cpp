#include <gtest/gtest.h>

#include "execution/execution_engine.hpp"
#include "index/index_manager.hpp"
#include "storage/database.hpp"

#include <filesystem>
#include <string>

namespace querycraft::execution {

class ExecutionEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "querycraft_exec_test";
        std::error_code ec;
        std::filesystem::create_directories(test_dir_, ec);
        db_path_ = test_dir_ / "test_exec.db";
        std::filesystem::remove(db_path_, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
};

TEST_F(ExecutionEngineTest, CreateTableValidAndDuplicateRejection) {
    storage::Database db(db_path_);
    ExecutionEngine engine(db);

    auto res1 = engine.execute("CREATE TABLE Students (id INT PRIMARY KEY, name VARCHAR(100), gpa FLOAT);");
    EXPECT_TRUE(res1.status().ok());
    EXPECT_FALSE(res1.is_query());
    EXPECT_EQ(res1.message(), "Table created successfully.");
    EXPECT_TRUE(db.has_table("Students"));

    // Duplicate table rejection
    auto res2 = engine.execute("CREATE TABLE Students (id INT);");
    EXPECT_FALSE(res2.status().ok());
    EXPECT_EQ(res2.status().code(), common::StatusCode::AlreadyExists);

    // Duplicate column rejection
    auto res3 = engine.execute("CREATE TABLE Courses (cid INT, cid VARCHAR(50));");
    EXPECT_FALSE(res3.status().ok());
    EXPECT_EQ(res3.status().code(), common::StatusCode::InvalidArgument);
}

TEST_F(ExecutionEngineTest, InsertAndSelectWorkflows) {
    storage::Database db(db_path_);
    ExecutionEngine engine(db);

    ASSERT_TRUE(engine.execute("CREATE TABLE Users (id INT PRIMARY KEY, name VARCHAR(50), age INT);").status().ok());

    // Valid INSERT statements
    auto ins1 = engine.execute("INSERT INTO Users VALUES (1, 'Alice', 20);");
    ASSERT_TRUE(ins1.status().ok());
    EXPECT_EQ(ins1.rows_affected(), 1);

    auto ins2 = engine.execute("INSERT INTO Users VALUES (2, 'Bob', 25);");
    ASSERT_TRUE(ins2.status().ok());

    auto ins3 = engine.execute("INSERT INTO Users VALUES (3, 'Charlie', 30);");
    ASSERT_TRUE(ins3.status().ok());

    // SELECT *
    auto sel_all = engine.execute("SELECT * FROM Users;");
    ASSERT_TRUE(sel_all.status().ok());
    EXPECT_TRUE(sel_all.is_query());
    EXPECT_EQ(sel_all.row_count(), 3);
    EXPECT_EQ(sel_all.column_names().size(), 3);
    EXPECT_EQ(sel_all.column_names()[0], "id");
    EXPECT_EQ(sel_all.column_names()[1], "name");
    EXPECT_EQ(sel_all.column_names()[2], "age");

    // SELECT with specific projections and WHERE
    auto sel_proj = engine.execute("SELECT name, age FROM Users WHERE age > 22;");
    ASSERT_TRUE(sel_proj.status().ok());
    EXPECT_EQ(sel_proj.row_count(), 2);
    EXPECT_EQ(sel_proj.column_names().size(), 2);
    EXPECT_EQ(sel_proj.column_names()[0], "name");
    EXPECT_EQ(sel_proj.column_names()[1], "age");

    EXPECT_EQ(sel_proj.rows()[0][0].as_string(), "Bob");
    EXPECT_EQ(sel_proj.rows()[0][1].as_int(), 25);
    EXPECT_EQ(sel_proj.rows()[1][0].as_string(), "Charlie");
    EXPECT_EQ(sel_proj.rows()[1][1].as_int(), 30);
}

TEST_F(ExecutionEngineTest, InsertConstraintsAndValidation) {
    storage::Database db(db_path_);
    ExecutionEngine engine(db);

    ASSERT_TRUE(engine.execute("CREATE TABLE Items (id INT PRIMARY KEY, name VARCHAR(30), price FLOAT);").status().ok());

    // Value count mismatch
    auto res_few = engine.execute("INSERT INTO Items VALUES (1, 'Laptop');");
    EXPECT_FALSE(res_few.status().ok());
    EXPECT_EQ(res_few.status().code(), common::StatusCode::InvalidArgument);

    // Type mismatch: string into INT
    auto res_type = engine.execute("INSERT INTO Items VALUES ('one', 'Laptop', 999.99);");
    EXPECT_FALSE(res_type.status().ok());
    EXPECT_EQ(res_type.status().code(), common::StatusCode::InvalidArgument);

    // Primary key cannot be NULL
    auto res_null = engine.execute("INSERT INTO Items VALUES (NULL, 'Laptop', 999.99);");
    EXPECT_FALSE(res_null.status().ok());
    EXPECT_EQ(res_null.status().code(), common::StatusCode::InvalidArgument);

    // Explicit column list with omitted nullable column
    auto res_explicit = engine.execute("INSERT INTO Items (id, name) VALUES (10, 'Mouse');");
    ASSERT_TRUE(res_explicit.status().ok());

    auto sel = engine.execute("SELECT * FROM Items WHERE id = 10;");
    ASSERT_TRUE(sel.status().ok());
    ASSERT_EQ(sel.row_count(), 1);
    EXPECT_EQ(sel.rows()[0][0].as_int(), 10);
    EXPECT_EQ(sel.rows()[0][1].as_string(), "Mouse");
    EXPECT_TRUE(sel.rows()[0][2].is_null()); // Omitted price column filled with NULL
}

TEST_F(ExecutionEngineTest, SelectFilteringAndLimit) {
    storage::Database db(db_path_);
    ExecutionEngine engine(db);

    ASSERT_TRUE(engine.execute("CREATE TABLE Employees (id INT PRIMARY KEY, dept VARCHAR(20), salary FLOAT);").status().ok());

    ASSERT_TRUE(engine.execute("INSERT INTO Employees VALUES (1, 'Engineering', 90000.0);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Employees VALUES (2, 'HR', 65000.0);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Employees VALUES (3, 'Engineering', 120000.0);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Employees VALUES (4, 'Sales', 75000.0);").status().ok());

    // AND condition
    auto res_and = engine.execute("SELECT * FROM Employees WHERE dept = 'Engineering' AND salary > 100000.0;");
    ASSERT_TRUE(res_and.status().ok());
    EXPECT_EQ(res_and.row_count(), 1);
    EXPECT_EQ(res_and.rows()[0][0].as_int(), 3);

    // OR condition
    auto res_or = engine.execute("SELECT * FROM Employees WHERE dept = 'HR' OR salary > 100000.0;");
    ASSERT_TRUE(res_or.status().ok());
    EXPECT_EQ(res_or.row_count(), 2);

    // LIMIT
    auto res_limit = engine.execute("SELECT * FROM Employees LIMIT 2;");
    ASSERT_TRUE(res_limit.status().ok());
    EXPECT_EQ(res_limit.row_count(), 2);

    // Empty result set
    auto res_empty = engine.execute("SELECT * FROM Employees WHERE salary > 999999.0;");
    ASSERT_TRUE(res_empty.status().ok());
    EXPECT_EQ(res_empty.row_count(), 0);

    // Non-existent table
    auto res_no_tbl = engine.execute("SELECT * FROM NonExistent;");
    EXPECT_FALSE(res_no_tbl.status().ok());
    EXPECT_EQ(res_no_tbl.status().code(), common::StatusCode::NotFound);

    // Non-existent column
    auto res_no_col = engine.execute("SELECT bogus_column FROM Employees;");
    EXPECT_FALSE(res_no_col.status().ok());
    EXPECT_EQ(res_no_col.status().code(), common::StatusCode::NotFound);
}

TEST_F(ExecutionEngineTest, UpdateStatementExecution) {
    storage::Database db(db_path_);
    ExecutionEngine engine(db);

    ASSERT_TRUE(engine.execute("CREATE TABLE Students (id INT PRIMARY KEY, name VARCHAR(50), age INT);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Students VALUES (1, 'Alice', 20);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Students VALUES (2, 'Bob', 22);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Students VALUES (3, 'Charlie', 20);").status().ok());

    // Update single row
    auto upd1 = engine.execute("UPDATE Students SET age = 23 WHERE id = 2;");
    ASSERT_TRUE(upd1.status().ok());
    EXPECT_EQ(upd1.rows_affected(), 1);

    auto sel1 = engine.execute("SELECT age FROM Students WHERE id = 2;");
    ASSERT_TRUE(sel1.status().ok());
    ASSERT_EQ(sel1.row_count(), 1);
    EXPECT_EQ(sel1.rows()[0][0].as_int(), 23);

    // Update multiple matching rows
    auto upd2 = engine.execute("UPDATE Students SET age = 21 WHERE age = 20;");
    ASSERT_TRUE(upd2.status().ok());
    EXPECT_EQ(upd2.rows_affected(), 2);

    auto sel2 = engine.execute("SELECT * FROM Students WHERE age = 21;");
    ASSERT_TRUE(sel2.status().ok());
    EXPECT_EQ(sel2.row_count(), 2);

    // Update with 0 matching rows
    auto upd3 = engine.execute("UPDATE Students SET age = 99 WHERE id = 999;");
    ASSERT_TRUE(upd3.status().ok());
    EXPECT_EQ(upd3.rows_affected(), 0);
}

TEST_F(ExecutionEngineTest, DeleteStatementExecution) {
    storage::Database db(db_path_);
    ExecutionEngine engine(db);

    ASSERT_TRUE(engine.execute("CREATE TABLE Tasks (id INT PRIMARY KEY, title VARCHAR(50), status VARCHAR(20));").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Tasks VALUES (1, 'Design', 'Done');").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Tasks VALUES (2, 'Code', 'InProgress');").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Tasks VALUES (3, 'Test', 'Done');").status().ok());

    // Delete matching rows
    auto del1 = engine.execute("DELETE FROM Tasks WHERE status = 'Done';");
    ASSERT_TRUE(del1.status().ok());
    EXPECT_EQ(del1.rows_affected(), 2);

    auto sel1 = engine.execute("SELECT * FROM Tasks;");
    ASSERT_TRUE(sel1.status().ok());
    ASSERT_EQ(sel1.row_count(), 1);
    EXPECT_EQ(sel1.rows()[0][0].as_int(), 2);
    EXPECT_EQ(sel1.rows()[0][1].as_string(), "Code");

    // Delete with 0 matching rows
    auto del2 = engine.execute("DELETE FROM Tasks WHERE id = 999;");
    ASSERT_TRUE(del2.status().ok());
    EXPECT_EQ(del2.rows_affected(), 0);

    // Unconditional DELETE deletes all remaining rows
    auto del_all = engine.execute("DELETE FROM Tasks;");
    ASSERT_TRUE(del_all.status().ok());
    EXPECT_EQ(del_all.rows_affected(), 1);

    auto sel_empty = engine.execute("SELECT * FROM Tasks;");
    ASSERT_TRUE(sel_empty.status().ok());
    EXPECT_EQ(sel_empty.row_count(), 0);
}

TEST_F(ExecutionEngineTest, CreateIndexAndMaintainConsistencyAcrossCrud) {
    storage::Database db(db_path_);
    index::IndexManager im(db.disk_manager());
    ExecutionEngine engine(db, &im);

    ASSERT_TRUE(engine.execute("CREATE TABLE Products (id INT PRIMARY KEY, name VARCHAR(50), price FLOAT);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Products VALUES (1, 'Keyboard', 49.99);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Products VALUES (2, 'Mouse', 24.99);").status().ok());
    ASSERT_TRUE(engine.execute("INSERT INTO Products VALUES (3, 'Monitor', 199.99);").status().ok());

    // CREATE INDEX on existing table
    auto idx_res = engine.execute("CREATE INDEX idx_products_price ON Products(price);");
    ASSERT_TRUE(idx_res.status().ok());
    EXPECT_TRUE(im.has_index("idx_products_price"));

    // Verify index was populated with the existing 3 records
    auto search_init = im.search("idx_products_price", index::IndexKey(49.99));
    ASSERT_EQ(search_init.size(), 1);

    // 1. INSERT maintenance: inserting a new product must update the index
    ASSERT_TRUE(engine.execute("INSERT INTO Products VALUES (4, 'Headset', 79.99);").status().ok());
    auto search_ins = im.search("idx_products_price", index::IndexKey(79.99));
    ASSERT_EQ(search_ins.size(), 1);

    // 2. UPDATE maintenance: updating price from 24.99 to 29.99
    ASSERT_TRUE(engine.execute("UPDATE Products SET price = 29.99 WHERE id = 2;").status().ok());
    // Old price 24.99 should no longer exist in index
    auto search_old = im.search("idx_products_price", index::IndexKey(24.99));
    EXPECT_EQ(search_old.size(), 0);
    // New price 29.99 must exist in index
    auto search_new = im.search("idx_products_price", index::IndexKey(29.99));
    ASSERT_EQ(search_new.size(), 1);

    // 3. DELETE maintenance: deleting product 1 should remove 49.99 from index
    ASSERT_TRUE(engine.execute("DELETE FROM Products WHERE id = 1;").status().ok());
    auto search_del = im.search("idx_products_price", index::IndexKey(49.99));
    EXPECT_EQ(search_del.size(), 0);
}

TEST_F(ExecutionEngineTest, FullSessionRestartRecovery) {
    // Session 1: Create table, insert records, create index, update, flush
    {
        storage::Database db(db_path_);
        index::IndexManager im(db.disk_manager());
        ExecutionEngine engine(db, &im);

        ASSERT_TRUE(engine.execute("CREATE TABLE Inventory (item_id INT PRIMARY KEY, qty INT);").status().ok());
        ASSERT_TRUE(engine.execute("INSERT INTO Inventory VALUES (101, 50);").status().ok());
        ASSERT_TRUE(engine.execute("INSERT INTO Inventory VALUES (102, 100);").status().ok());
        ASSERT_TRUE(engine.execute("CREATE INDEX idx_inv_qty ON Inventory(qty);").status().ok());
        ASSERT_TRUE(engine.execute("INSERT INTO Inventory VALUES (103, 75);").status().ok());

        db.close();
    }

    // Session 2: Reopen database and auto_load indexes
    {
        storage::Database db(db_path_);
        ASSERT_TRUE(db.is_open());
        EXPECT_TRUE(db.has_table("Inventory"));

        index::IndexManager im(db.disk_manager());
        ASSERT_TRUE(im.auto_load().ok());
        EXPECT_TRUE(im.has_index("idx_inv_qty"));

        ExecutionEngine engine(db, &im);

        // SELECT records to verify data persisted
        auto sel = engine.execute("SELECT * FROM Inventory WHERE qty >= 75;");
        ASSERT_TRUE(sel.status().ok());
        EXPECT_EQ(sel.row_count(), 2);

        // Verify index search on recovered B+ Tree
        auto idx_res = im.search("idx_inv_qty", index::IndexKey(static_cast<std::int64_t>(100)));
        ASSERT_EQ(idx_res.size(), 1);
    }
}

} // namespace querycraft::execution
