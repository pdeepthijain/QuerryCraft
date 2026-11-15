#include "common/shell.hpp"

#include <iostream>

int main() {
    try {
        querycraft::common::Shell shell;
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
