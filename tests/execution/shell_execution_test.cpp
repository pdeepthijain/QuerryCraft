#include <gtest/gtest.h>

#include "common/shell.hpp"
#include "execution/execution_engine.hpp"
#include "index/index_manager.hpp"
#include "storage/database.hpp"

#include <filesystem>
#include <sstream>
#include <string>

namespace querycraft::execution {

class ShellExecutionTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "querycraft_shell_exec_test";
        std::error_code ec;
        std::filesystem::create_directories(test_dir_, ec);
        db_path_ = test_dir_ / "test_shell.db";
        std::filesystem::remove(db_path_, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
};

TEST_F(ShellExecutionTest, InteractiveSessionFullSqlWorkflow) {
    storage::Database db(db_path_);
    index::IndexManager im(db.disk_manager());
    ExecutionEngine engine(db, &im);

    std::string session_input =
        "CREATE TABLE Students (id INT, name VARCHAR(100), age INT);\n"
        "INSERT INTO Students VALUES (1, 'Asha', 19);\n"
        "INSERT INTO Students VALUES (2, 'Rahul', 22);\n"
        "SELECT * FROM Students;\n"
        "SELECT name FROM Students WHERE age > 20;\n"
        "UPDATE Students SET age = 23 WHERE id = 2;\n"
        "DELETE FROM Students WHERE id = 1;\n"
        "EXIT\n";

    std::istringstream in(session_input);
    std::ostringstream out;
    std::ostringstream err;

    common::Shell shell(in, out, err, &engine);
    shell.run();

    std::string output = out.str();

    // Verify each expected interaction from prompt Section 7
    EXPECT_NE(output.find("Table created successfully."), std::string::npos);
    EXPECT_NE(output.find("1 row inserted."), std::string::npos);
    EXPECT_NE(output.find("Asha"), std::string::npos);
    EXPECT_NE(output.find("Rahul"), std::string::npos);
    EXPECT_NE(output.find("2 rows returned."), std::string::npos);
    EXPECT_NE(output.find("1 row returned."), std::string::npos);
    EXPECT_NE(output.find("1 row updated."), std::string::npos);
    EXPECT_NE(output.find("1 row deleted."), std::string::npos);
    EXPECT_NE(output.find("Exiting QueryCraft. Goodbye!"), std::string::npos);

    // Verify table structure with borders
    EXPECT_NE(output.find("+----+-------+-----+"), std::string::npos);
    EXPECT_NE(output.find("| id | name  | age |"), std::string::npos);
}

TEST_F(ShellExecutionTest, InteractiveShellErrorReporting) {
    storage::Database db(db_path_);
    ExecutionEngine engine(db);

    std::string session_input =
        "SELECT * FROM NonExistentTable;\n"
        "THIS IS NOT VALID SQL;\n"
        "QUIT\n";

    std::istringstream in(session_input);
    std::ostringstream out;
    std::ostringstream err;

    common::Shell shell(in, out, err, &engine);
    shell.run();

    std::string err_output = err.str();
    EXPECT_NE(err_output.find("Error:"), std::string::npos);
    EXPECT_NE(err_output.find("NonExistentTable"), std::string::npos);
}

} // namespace querycraft::execution
