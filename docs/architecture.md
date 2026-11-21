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

## 3. Important Files & Modules

### Foundation & Common
- **`CMakeLists.txt`**: Central CMake configuration. Configures C++20, rigorous compiler warnings (`-Wall -Wextra -Wpedantic`, etc.), GoogleTest integration via FetchContent, AddressSanitizer options (`ENABLE_ASAN`), and modular targets.
- **`include/common/config.hpp`**: Engine-wide configuration constants including version, page size (`PAGE_SIZE = 4096`), sentinel values (`INVALID_PAGE_ID`, `INVALID_TXN_ID`), and type aliases (`page_id_t`, `txn_id_t`, `lsn_t`).
- **`include/common/status.hpp`**: Robust `Status` class modeled after modern systems engines (LevelDB/DuckDB/BusTub), providing explicit error checking with `StatusCode` values (`Ok`, `NotFound`, `AlreadyExists`, `InvalidArgument`, `NotSupported`, `IoError`, `InternalError`, `SyntaxError`) and human-readable messages.
- **`include/common/shell.hpp` & `src/common/shell.cpp`**: Interactive command-line REPL shell decoupled from the engine internals. Accepts streams (`std::istream`, `std::ostream`) for full unit testability.
- **`src/main.cpp`**: QueryCraft binary entry point. Instantiates and executes the interactive shell with exception isolation.

### SQL Lexer (Tokenizer)
- **`include/parser/token.hpp`**: `TokenType` enum covering 34 keywords, literals, operators, and punctuation; `SourceLocation` coordinate tracking (`line`, `column`); and the immutable `Token` class.
- **`include/parser/lexer.hpp` & `src/parser/lexer.cpp`**: State machine scanner supporting case-insensitive keyword lookup, arbitrary identifiers, integer/float literals, string escaping (`''`), `--` comments, and single-token lookahead (`peek_token()`).

### SQL Parser & Abstract Syntax Tree (AST)
- **`include/parser/ast.hpp` & `src/parser/ast.cpp`**:
  - `AstNode`: Abstract base class with source coordinates and `to_string()`.
  - `Expression`: Abstract base for expressions, supporting `clone()`.
    - `LiteralExpression`: Polymorphic variant for INT, FLOAT, STRING, NULL, and BOOL.
    - `ColumnRefExpression`: Column name with optional qualified table name.
    - `BinaryExpression`: Arithmetic (`+`, `-`, `*`, `/`), Comparison (`=`, `!=`, `<`, `<=`, `>`, `>=`), and Logical (`AND`, `OR`).
    - `UnaryExpression`: Unary operators (`+`, `-`, `NOT`).
  - `Statement`: Base class for top-level DDL and DML operations.
    - `SelectStatement`: Selected columns / `*`, FROM table, WHERE condition, ORDER BY (`ASC`/`DESC`), and LIMIT.
    - `CreateTableStatement`: Column definitions (`INT`, `FLOAT`, `TEXT`, `VARCHAR`), column-level and table-level PRIMARY KEY constraints.
    - `InsertStatement`: Target table, optional column list, and VALUES list.
    - `UpdateStatement`: Target table, SET assignments (`col = expr`), and optional WHERE clause.
    - `DeleteStatement`: Target table and optional WHERE clause.
    - `CreateIndexStatement`: Index name, target table, and indexed column list.
  - `ast_debug_print()`: Pretty tree debug printer with visual indentation for AST visualization.
- **`include/parser/parser.hpp` & `src/parser/parser.cpp`**:
  - Recursive-descent parser converting lexer tokens into strongly typed AST nodes.
  - Stratified operator precedence hierarchy:
    `Primary` -> `Unary` (`+`, `-`) -> `Multiplicative` (`*`, `/`) -> `Additive` (`+`, `-`) -> `Comparison` (`=`, `!=`, `<`, `<=`, `>`, `>=`) -> `Unary NOT` -> `Logical AND` -> `Logical OR`.
  - Diagnostic error reporting with line/column coordinates via `Status::SyntaxError`.

### Persistent Storage Engine
- **`include/storage/page.hpp` & `src/storage/page.cpp`**:
  - Fixed-size 4096-byte page abstraction with boundary-checked accessors (`read_bytes()`, `write_bytes()`).
  - Fixed 20-byte header:
    - `[0..3]`: `page_id` (`int32_t`)
    - `[4..5]`: `page_type` (`uint16_t`: `FREE_PAGE=0`, `CATALOG_PAGE=1`, `TABLE_DATA_PAGE=2`)
    - `[6..7]`: `num_slots` (`uint16_t`)
    - `[8..9]`: `free_space_pointer` (`uint16_t`, starts at 4096 and grows downwards)
    - `[10..13]`: `prev_page_id` (`int32_t`)
    - `[14..17]`: `next_page_id` (`int32_t`)
    - `[18..19]`: `flags` (`uint16_t`)
  - Slotted-page layout: Slot directory begins at offset 20 (`offset: uint16_t, length: uint16_t`, 4 bytes per slot). Record bytes grow from offset 4096 backwards towards the directory.
  - Slot reuse for deleted records (`offset=0, length=0`), full page rejection without corruption, and `compact()` defragmentation algorithm.
- **`include/storage/schema.hpp` & `src/storage/schema.cpp`**:
  - Column definitions (`name`, `DataType` (`INT`, `FLOAT`, `TEXT`, `VARCHAR`), `is_primary_key`, `is_nullable`, `length`).
  - Schema binary serialization and deserialization for catalog persistence.
  - Independent of SQL parsing layer.
- **`include/storage/record.hpp` & `src/storage/record.cpp`**:
  - `Value`: `std::variant<std::monostate, int64_t, double, std::string>` representing NULL, INT, FLOAT, and TEXT/VARCHAR.
  - `Record`: Vector of `Value` objects indexed by schema columns, with `RecordId {page_id, slot_id}`.
  - Schema constraint validation (`validate(schema)` checking types, nullability, primary key constraints, and VARCHAR lengths).
  - Robust binary serialization protocol: 2-byte value count, type tags (`0=NULL`, `1=INT [8B]`, `2=FLOAT [8B]`, `3=STRING [4B len + chars]`).
- **`include/storage/disk_manager.hpp` & `src/storage/disk_manager.cpp`**:
  - Low-level physical file I/O operations for binary database files in `data/`.
  - Fixed 4096-byte page read/write (`read_page()`, `write_page()`), page allocation (`allocate_page()`), file sizing, and `flush()`.
- **`include/storage/table.hpp` & `src/storage/table.cpp`**:
  - Doubly-linked chain of slotted data pages (`first_page_id`, `last_page_id`).
  - CRUD operations: `insert_record` (auto-allocating chained pages when full), `get_record`, `update_record` (in-place or reallocated), `delete_record`, and full sequential `scan_records`.
- **`include/storage/database.hpp` & `src/storage/database.cpp`**:
  - Central multi-table manager and catalog engine.
  - **Catalog Page (Page 0)** binary structure:
    - `[0..3]`: Magic signature `0x51435246` (`"QCRF"`)
    - `[4..5]`: Catalog version `1`
    - `[6..7]`: Table count
    - Table directory: table names, page chains (`first_page_id`, `last_page_id`), and serialized `Schema` binary representations.
  - Automatic catalog persistence on modifications and recovery across application restarts.

### B+ Tree Indexing Module
- **`include/index/index_key.hpp` & `src/index/index_key.cpp`**:
  - Strongly-typed `IndexKey` representing `INT` (`int64_t`), `FLOAT` (`double`), and `TEXT` (`std::string`).
  - Type-safe comparison: consistent equality, total ordering, and rejection of cross-type comparisons (`std::invalid_argument`).
  - Explicit `NULL` handling: `NULL` keys sort strictly before all non-null values (`NULLS FIRST`), adhering to relational semantics.
  - Type-tagged binary serialization into byte buffers and disk pages.
- **`include/index/bplus_tree_node.hpp` & `src/index/bplus_tree_node.cpp`**:
  - `BPlusTreeNode` with configurable degree (`max_keys`, default 4 for testing, configurable per index).
  - Leaf Nodes: sorted distinct keys, each mapping to a `std::vector<storage::RecordId>` to store duplicates without key duplication. Linked sequentially via `next_leaf` and `prev_leaf`.
  - Internal Nodes: $K$ separator keys and $K+1$ child pointers / child page IDs.
  - Binary 4KB page serialization (`INDEX_PAGE = 3`): header with node flags, parent PID, leaf links, and serialized key/rid payload.
- **`include/index/bplus_tree.hpp` & `src/index/bplus_tree.cpp`**:
  - Self-balancing search tree with $O(\log N)$ point lookups, insertions, and deletions.
  - Node Splitting: leaf split pushes up first key of right leaf; internal split pushes median key up to parent; root splits increase tree height by 1.
  - Node Underflow & Deletion: borrows from left sibling or right sibling; if siblings cannot lend, merges with sibling through parent separator; root shrinks when internal root becomes empty, decreasing tree height by 1.
  - Range Search: locates lower bound in $O(\log N)$ and traverses doubly-linked leaf nodes until upper bound is exceeded.
  - Debug Tree Invariant Validator (`validate_tree()`): asserts strictly sorted keys, child separator bounds, identical leaf depths, leaf link consistency, and minimum node occupancies.
  - Disk Persistence: serializes nodes into 4KB pages via `DiskManager` and reconstructs trees upon reload.
- **`include/index/index_manager.hpp` & `src/index/index_manager.cpp`**:
  - Manages column-level B+ Tree indexes for database tables.
  - Enforces key type matching on insertions and deletions.
  - **Index Catalog Page** binary format:
    - `[0..3]`: Magic signature `0x51434958` (`"QCIX"`)
    - `[4..5]`: Index catalog version `1`
    - `[6..7]`: Number of indexes
    - Index Directory: index name, table name, column name, key type, root page ID, and max keys.
  - Full persistence and recovery of all table indexes across database restart sessions.

---

## 4. Test Suites

- **`tests/common/shell_test.cpp`**: GoogleTest suite verifying shell input trimming, case handling, lifecycle, command display, and termination.
- **`tests/common/status_test.cpp`**: GoogleTest suite validating `Status` behavior across all status codes and messages.
- **`tests/parser/lexer_test.cpp`**: 22 unit tests verifying token classification, numbers, comments, strings, operators, and coordinates.
- **`tests/parser/ast_test.cpp`**: 14 unit tests verifying AST node creation, cloning, string formatting, and debug printing.
- **`tests/parser/parser_test.cpp`**: 28 unit tests covering all statement types, expression precedence, parenthesization, and syntax error diagnostics.
- **`tests/storage/page_test.cpp`**: 9 unit tests verifying 4096-byte page size, boundary checks, slotted inserts/reads, deleted slot reuse, compaction, and full-page rejection.
- **`tests/storage/record_test.cpp`**: 11 unit tests verifying `Value` variants, integer/float/string/NULL binary serialization, and schema constraint validation.
- **`tests/storage/disk_manager_test.cpp`**: 6 unit tests verifying database file creation, page allocation, reading/writing pages, persistence across instances, and boundary conditions.
- **`tests/storage/database_test.cpp`**: 8 unit tests verifying table creation, duplicate rejection, CRUD record operations, full multi-page scans, and catalog/schema/record persistence across application restarts.
- **`tests/index/bplus_tree_test.cpp`**: 9 unit tests verifying point search, sorted/reverse insertions, duplicate key handling, leaf/internal splits, leaf/internal merges, leaf borrowing, root shrinking, range queries, float/text/NULL types, randomized stress testing vs `std::map` reference model, tree invariant validation, and disk persistence.
- **`tests/index/index_manager_test.cpp`**: 6 unit tests verifying index creation, duplicate name/column rejection, CRUD entries, type mismatch validation, index dropping, and multi-index catalog persistence across restart sessions.

