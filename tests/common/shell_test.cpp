#include <gtest/gtest.h>
#include "common/shell.hpp"

#include <sstream>

using namespace querycraft::common;

TEST(ShellTest, TrimLeadingAndTrailingWhitespace) {
    EXPECT_EQ(Shell::trim(""), "");
    EXPECT_EQ(Shell::trim("   "), "");
    EXPECT_EQ(Shell::trim("\t\n\r  \v\f"), "");
    EXPECT_EQ(Shell::trim("SELECT 1;"), "SELECT 1;");
    EXPECT_EQ(Shell::trim("   SELECT 1;"), "SELECT 1;");
    EXPECT_EQ(Shell::trim("SELECT 1;   "), "SELECT 1;");
    EXPECT_EQ(Shell::trim("  \t SELECT 1; \n\r "), "SELECT 1;");
    EXPECT_EQ(Shell::trim("A"), "A");
    EXPECT_EQ(Shell::trim("   A   "), "A");
}

TEST(ShellTest, ToUpperConversion) {
    EXPECT_EQ(Shell::to_upper(""), "");
    EXPECT_EQ(Shell::to_upper("exit"), "EXIT");
    EXPECT_EQ(Shell::to_upper("Quit"), "QUIT");
    EXPECT_EQ(Shell::to_upper(".exit"), ".EXIT");
    EXPECT_EQ(Shell::to_upper("select * from t;"), "SELECT * FROM T;");
    EXPECT_EQ(Shell::to_upper("AlreadyUpper123"), "ALREADYUPPER123");
}

TEST(ShellTest, ProcessEmptyInputGracefully) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    Shell shell(in, out, err);

    EXPECT_EQ(shell.process_line(""), ShellAction::Continue);
    EXPECT_EQ(shell.process_line("   "), ShellAction::Continue);
    EXPECT_EQ(shell.process_line("\t\n"), ShellAction::Continue);
    EXPECT_TRUE(out.str().empty());
}

TEST(ShellTest, ProcessExitAndQuitCommands) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    Shell shell(in, out, err);

    EXPECT_EQ(shell.process_line("EXIT"), ShellAction::Exit);
    EXPECT_EQ(shell.process_line("exit"), ShellAction::Exit);
    EXPECT_EQ(shell.process_line("QUIT"), ShellAction::Exit);
    EXPECT_EQ(shell.process_line("quit"), ShellAction::Exit);
    EXPECT_EQ(shell.process_line("  .exit  "), ShellAction::Exit);
    EXPECT_EQ(shell.process_line(".quit"), ShellAction::Exit);

    EXPECT_NE(out.str().find("Exiting QueryCraft. Goodbye!"), std::string::npos);
}

TEST(ShellTest, ProcessHelpCommand) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    Shell shell(in, out, err);

    EXPECT_EQ(shell.process_line("HELP"), ShellAction::Continue);
    EXPECT_NE(out.str().find("QueryCraft Interactive Shell Help"), std::string::npos);

    out.str("");
    EXPECT_EQ(shell.process_line(".help"), ShellAction::Continue);
    EXPECT_NE(out.str().find("QueryCraft Interactive Shell Help"), std::string::npos);
}

TEST(ShellTest, ProcessDisplayEnteredCommand) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    Shell shell(in, out, err);

    std::string test_cmd = "CREATE TABLE users (id INT, name VARCHAR);";
    EXPECT_EQ(shell.process_line(test_cmd), ShellAction::Continue);

    std::string output = out.str();
    EXPECT_NE(output.find("Entered command: CREATE TABLE users (id INT, name VARCHAR);"), std::string::npos);
    EXPECT_NE(output.find("Foundation phase"), std::string::npos);
}

TEST(ShellTest, InteractiveSessionLifecycle) {
    std::string session_input =
        "\n"
        "   \n"
        "HELP\n"
        "SELECT * FROM test_table;\n"
        "EXIT\n";

    std::istringstream in(session_input);
    std::ostringstream out;
    std::ostringstream err;
    Shell shell(in, out, err);

    shell.run();

    std::string output = out.str();
    // Verify welcome banner
    EXPECT_NE(output.find("QueryCraft Database Engine"), std::string::npos);
    // Verify prompt was printed
    EXPECT_NE(output.find("querycraft> "), std::string::npos);
    // Verify help was printed
    EXPECT_NE(output.find("QueryCraft Interactive Shell Help"), std::string::npos);
    // Verify command was displayed
    EXPECT_NE(output.find("Entered command: SELECT * FROM test_table;"), std::string::npos);
    // Verify goodbye message
    EXPECT_NE(output.find("Exiting QueryCraft. Goodbye!"), std::string::npos);
}
