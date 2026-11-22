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

### Buffer Pool Manager & LRU Replacement Policy
- **`include/buffer/lru_replacer.hpp` & `src/buffer/lru_replacer.cpp`**:
  - In-memory eviction tracking implementing the Least-Recently-Used (LRU) policy.
  - Constant time $O(1)$ operations via combination of `std::list<frame_id_t>` and `std::unordered_map<frame_id_t, list::iterator>`.
  - Operations:
    - `Victim(frame_id*)`: Selects least recently used unpinned frame for eviction.
    - `Pin(frame_id)`: Removes frame from eviction consideration when pinned by a thread/query.
    - `Unpin(frame_id)`: Re-adds frame to MRU position when pin count drops to 0. Duplicate unpins are safely ignored.
    - `RecordAccess(frame_id)`: Moves evictable frame to the MRU position.
    - `Size()` & `Capacity()`: Tracks active evictable frames.
- **`include/buffer/buffer_pool_manager.hpp` & `src/buffer/buffer_pool_manager.cpp`**:
  - Memory manager caching 4096-byte pages in memory frames.
  - Components:
    - Array of `Frame` structs holding `Page`, `page_id`, `pin_count`, `is_dirty`, and `is_occupied`.
    - Page table (`std::unordered_map<page_id_t, frame_id_t>`) mapping disk page IDs to memory frame indices.
    - Free list (`std::vector<frame_id_t>`) tracking unoccupied frames.
    - `LRUReplacer` managing victim candidate selection.
  - Core Operations:
    - `FetchPage(page_id)`: Returns in-memory page; reads from disk on miss; manages pin count and LRU status.
    - `UnpinPage(page_id, is_dirty)`: Decrements pin count; updates dirty bit; enables LRU eviction when pin count reaches 0.
    - `NewPage(out_page_id, type)`: Allocates new page on disk and installs it in an available frame with pin count 1.
    - `DeletePage(page_id)`: Frees frame if page is unpinned; returns frame to free list.
    - `FlushPage(page_id)`: Writes dirty page to disk, clears dirty bit, and flushes underlying file.
    - `FlushAllPages()`: Flushes all dirty frames to disk and calls `DiskManager::flush()`.
  - Telemetry: Tracks `num_reads`, `num_writes`, `num_hits`, `num_misses`.
  - Integration: Integrated with `BPlusTree` and `IndexManager` persistence and restart recovery.

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

### SQL Execution Engine
- **`include/execution/execution_context.hpp`**:
  - Encapsulates execution state: references to `Database`, optional `IndexManager`, and optional `BufferPoolManager`.
- **`include/execution/execution_result.hpp` & `src/execution/execution_result.cpp`**:
  - Structured output representing query results and command outcomes.
  - Carries `Status`, `is_query`, column names, column data types, row vectors of `Value`, `rows_affected`, and human-readable feedback.
  - Implements aesthetic ASCII table formatting (`+----+----+`) with dynamic column width calculation.
- **`include/execution/expression_evaluator.hpp` & `src/execution/expression_evaluator.cpp`**:
  - Dynamically evaluates parsed AST expressions (`LiteralExpression`, `ColumnRefExpression`, `UnaryExpression`, `BinaryExpression`).
  - Arithmetic operators (`+`, `-`, `*`, `/`) with division-by-zero checks and integer/floating-point promotion.
  - Comparison operators (`=`, `!=`, `<`, `<=`, `>`, `>=`) with type-safe comparisons and SQL 3-valued NULL logic (comparisons involving NULL yield NULL).
  - Logical operators (`AND`, `OR`) with short-circuit evaluation.
  - Unary operators (`+`, `-`, `NOT`).
  - Column reference resolution against active record and table schema.
- **Statement Executors**:
  - **`CreateTableExecutor`**: Validates table names, column constraints, primary keys, and types; constructs `Schema` and calls `Database::create_table()`.
  - **`InsertExecutor`**: Validates column counts and types; supports full-row and explicit-column syntax; sets omitted nullable fields to NULL; validates NOT NULL constraints; inserts records and updates all corresponding B+ Tree indexes.
  - **`SelectExecutor`**: Reads records via sequential scan; evaluates WHERE clauses with `ExpressionEvaluator`; applies column projections; handles LIMIT truncation.
  - **`UpdateExecutor`**: Evaluates WHERE filter; updates columns in-place; validates schema constraints; updates B+ Tree indexes if indexed values change.
  - **`DeleteExecutor`**: Evaluates WHERE filter; deletes matching records; removes corresponding entries from all table indexes.
  - **`CreateIndexExecutor`**: Validates table, column, and data type; creates persistent B+ Tree in `IndexManager`; populates index from existing records; persists catalog.
- **`include/execution/execution_engine.hpp` & `src/execution/execution_engine.cpp`**:
  - Top-level query orchestrator dispatching parsed AST statements to their respective executors.

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
- **`tests/buffer/lru_replacer_test.cpp`**: 8 unit tests verifying LRU victim order, pin protection against eviction, unpin MRU position, access recording, duplicate unpin handling, out-of-range bounds, size, and capacity.
- **`tests/buffer/buffer_pool_manager_test.cpp`**: 18 unit tests verifying NewPage, FetchPage, cache hits avoiding disk reads, pin count decrement, dirty flag setting, FlushPage, FlushAllPages, DeletePage (pinned vs unpinned), small pool (2-3 frame) eviction, dirty flushing on eviction, clean page eviction without disk writes, all-pinned allocation failure, pinned page eviction immunity, LRU ordering, restart recovery, B+ Tree integration, IndexManager integration, and slotted-page record persistence.
- **`tests/index/bplus_tree_test.cpp`**: 9 unit tests verifying point search, sorted/reverse insertions, duplicate key handling, leaf/internal splits, leaf/internal merges, leaf borrowing, root shrinking, range queries, float/text/NULL types, randomized stress testing vs `std::map` reference model, tree invariant validation, and disk persistence.
- **`tests/index/index_manager_test.cpp`**: 6 unit tests verifying index creation, duplicate name/column rejection, CRUD entries, type mismatch validation, index dropping, and multi-index catalog persistence across restart sessions.
- **`tests/execution/expression_evaluator_test.cpp`**: 7 unit tests covering literal evaluation, column references, arithmetic operations, comparisons, logical AND/OR/NOT, SQL 3-valued NULL semantics, and WHERE predicate evaluation.
- **`tests/execution/execution_engine_test.cpp`**: 8 unit tests covering CREATE TABLE, duplicate rejection, INSERT full and explicit columns, constraint checking, SELECT projections and WHERE filtering, LIMIT, UPDATE, DELETE, CREATE INDEX with B+ Tree synchronization, and full session restart recovery.
- **`tests/execution/shell_execution_test.cpp`**: 2 integration tests validating complete interactive CLI session workflows, tabular ASCII formatting, and error reporting through the shell.

