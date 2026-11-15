# QueryCraft Architecture Documentation

## 1. Architectural Overview

QueryCraft is a modular relational database management system (RDBMS) engine engineered from scratch in modern C++20. It follows a classical layered database architecture similar to SQLite and academic systems such as CMU-DB BusTub.

```
+-------------------------------------------------------------+
|             Interactive Shell / CLI (querycraft)            |
+-------------------------------------------------------------+
                              |
+-------------------------------------------------------------+
|             SQL Parser & Lexer (include/parser)             |
|         Converts raw SQL statements into an AST             |
+-------------------------------------------------------------+
                              |
+-------------------------------------------------------------+
|          Catalog & Binder / Analyzer (include/catalog)      |
|    Resolves table, column, and schema object metadata       |
+-------------------------------------------------------------+
                              |
+-------------------------------------------------------------+
|            Query Optimizer (include/optimizer)              |
|        Transforms AST into an optimized physical plan       |
+-------------------------------------------------------------+
                              |
+-------------------------------------------------------------+
|            Execution Engine (include/execution)             |
|       Volcano / Iterator model (Next() based execution)     |
+-------------------------------------------------------------+
            |                                    |
+-----------------------+            +------------------------+
| Index (include/index) |            | Storage Engine         |
| B+ Tree index engine  |            | Slotted-page tables    |
+-----------------------+            +------------------------+
            \                                    /
+-------------------------------------------------------------+
|         Buffer Pool Manager (include/buffer)                |
|    Frame table, replacement policy (LRU-K), page caching    |
+-------------------------------------------------------------+
                              |
+-------------------------------------------------------------+
|             Disk Manager (include/storage)                  |
|       Page allocation, block read/write (4KB pages)         |
+-------------------------------------------------------------+
```

---

## 2. Directory & Module Responsibilities

| Directory | Layer | Purpose |
| :--- | :--- | :--- |
| `include/common/`, `src/common/` | Core Utilities | Engine-wide constants, status codes (`Status`), error types, strongly typed IDs, and REPL shell interface. |
| `include/parser/`, `src/parser/` | Front-end | Lexer and recursive-descent parser producing Abstract Syntax Trees (AST). |
| `include/optimizer/`, `src/optimizer/` | Query Planning | Rule-based and cost-based query plan optimization. |
| `include/execution/`, `src/execution/` | Execution Engine | Volcano-style iterator execution engine (`Init()`, `Next()`) executing physical plan nodes (SeqScan, Insert, Filter, Projection, HashJoin). |
| `include/index/`, `src/index/` | Indexing | B+ Tree index for primary keys and secondary lookups. |
| `include/buffer/`, `src/buffer/` | Memory Management | Buffer Pool Manager managing in-memory 4KB frames with LRU/LRU-K eviction. |
| `include/storage/`, `src/storage/` | Disk Storage | Slotted-page storage model, tuple serialization, and file I/O abstraction. |
| `tests/` | Verification | GoogleTest test suites covering components across layers. |
| `benchmarks/` | Performance | Microbenchmarks and throughput analysis. |
| `data/` | Persistence | Runtime database files, write-ahead logs, and schema metadata. |
| `docs/` | Documentation | Architecture specifications, design documents, and development notes. |

---

## 3. Important Files (Foundation Phase)

- **`CMakeLists.txt`**: Central CMake configuration. Configures C++20, rigorous compiler warnings (`-Wall -Wextra -Wpedantic`, etc.), GoogleTest integration via FetchContent, AddressSanitizer options (`ENABLE_ASAN`), and modular targets.
- **`include/common/config.hpp`**: Engine-wide configuration constants including version, page size (`PAGE_SIZE = 4096`), sentinel values (`INVALID_PAGE_ID`, `INVALID_TXN_ID`), and type aliases (`page_id_t`, `txn_id_t`, `lsn_t`).
- **`include/common/status.hpp`**: Robust `Status` class modeled after modern systems engines (LevelDB/DuckDB/BusTub), providing explicit error checking with `StatusCode` values and human-readable messages.
- **`include/common/shell.hpp` & `src/common/shell.cpp`**: Interactive command-line REPL shell decoupled from the engine internals. Accepts streams (`std::istream`, `std::ostream`) for full unit testability.
- **`src/main.cpp`**: QueryCraft binary entry point. Instantiates and executes the interactive shell with exception isolation.
- **`tests/common/shell_test.cpp`**: GoogleTest suite verifying shell input trimming, case handling, lifecycle, command display, and termination.
- **`tests/common/status_test.cpp`**: GoogleTest suite validating `Status` behavior across all status codes and messages.
- **`.gitignore`**: Excludes build artifacts, binaries, temporary cache files, runtime database files, and local tooling.
