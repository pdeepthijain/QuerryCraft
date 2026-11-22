#include "common/shell.hpp"
#include "execution/execution_engine.hpp"
#include "index/index_manager.hpp"
#include "storage/database.hpp"

#include <filesystem>
#include <iostream>

int main() {
    try {
        std::error_code ec;
        std::filesystem::create_directories("data", ec);
        querycraft::storage::Database db("data/querycraft.db");
        querycraft::index::IndexManager im(db.disk_manager());
        (void)im.auto_load();

        querycraft::execution::ExecutionEngine engine(db, &im);
        querycraft::common::Shell shell(std::cin, std::cout, std::cerr, &engine);
        shell.run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "QueryCraft fatal error: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "QueryCraft fatal error: unknown exception caught.\n";
        return 1;
    }
}
