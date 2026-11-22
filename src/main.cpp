#include "common/shell.hpp"
#include "execution/execution_engine.hpp"
#include "index/index_manager.hpp"
#include "storage/database.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        std::string db_path = "data/querycraft.db";
        if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
            db_path = argv[1];
        }
        std::error_code ec;
        std::filesystem::path p(db_path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        querycraft::storage::Database db(db_path);
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
