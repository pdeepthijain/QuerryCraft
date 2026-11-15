# QueryCraft — Relational Database Engine Built from Scratch

**QueryCraft** is a lightweight relational database management system (RDBMS) engine inspired by SQLite, written completely from scratch in modern **C++20**. Designed as a systems programming project, it implements fundamental database internals without relying on SQLite, MySQL, PostgreSQL, or any third-party database engines.

---

## Architecture & Roadmap

QueryCraft is structured into modular layers mirroring production-grade database systems:

```
[ Interactive REPL Shell ]
            |
[ SQL Lexer & Parser ]
            |
[ Catalog & Binder ]
            |
[ Query Optimizer ]
            |
[ Volcano-style Execution Engine ]
      /                  \
[ B+ Tree Index ]     [ Slotted-Page Storage ]
      \                  /
[ Buffer Pool Manager (LRU/LRU-K) ]
            |
[ Disk Manager (4KB Block I/O) ]
```

---

## Technology Stack

- **Language:** C++20 (`-std=c++20`)
- **Build System:** CMake (>= 3.20)
- **Compilers:** GCC 10+, Clang 12+, or MSVC 2019/2022
- **Testing Framework:** GoogleTest (v1.14.0 via CMake `FetchContent`)
- **Debugging & Sanitization:** AddressSanitizer (`-fsanitize=address`)
- **Tooling:** Ninja or GNU Make, Git

---

## Project Structure

```
QueryCraft/
├── CMakeLists.txt              # Root CMake build configuration
├── README.md                   # Project documentation
├── .gitignore                  # Git ignore rules for build artifacts & temp files
├── include/                    # Public C++ header files
│   ├── common/                 # Engine constants, types, status codes, shell interface
│   ├── parser/                 # SQL Token, Lexer, and AST & Parser interfaces
│   │   ├── token.hpp           # TokenType enum, SourceLocation, and Token class
│   │   ├── lexer.hpp           # Streaming Lexer tokenizer interface
│   │   ├── ast.hpp             # Abstract Syntax Tree hierarchy (Statements & Expressions)
│   │   └── parser.hpp          # Recursive-descent SQL Parser interface
│   ├── storage/                # Disk manager, page layout, tuple serialization (upcoming)
│   ├── index/                  # B+ Tree index structures (upcoming)
│   ├── execution/              # Volcano iterator execution model (upcoming)
│   ├── optimizer/              # Query plan representation and optimizer (upcoming)
│   └── buffer/                 # Buffer pool manager and frame eviction policies (upcoming)
├── src/                        # Implementation source files
│   ├── common/                 # Common module implementations (e.g. Shell)
│   ├── parser/                 # SQL Lexer and Parser implementations
│   │   ├── lexer.cpp           # Tokenizer state machine and keyword resolution
│   │   ├── ast.cpp             # AST stringification and tree debug printer
│   │   └── parser.cpp          # Recursive-descent parser & precedence hierarchy
│   ├── storage/                # Storage engine implementation (upcoming)
│   ├── index/                  # B+ Tree implementation (upcoming)
│   ├── execution/              # Execution operators implementation (upcoming)
│   ├── optimizer/              # Plan nodes and optimizer passes (upcoming)
│   ├── buffer/                 # Buffer pool implementation (upcoming)
│   └── main.cpp                # QueryCraft binary entry point
├── tests/                      # Automated test suite (GoogleTest)
│   ├── common/                 # Unit tests for common utilities and Shell
│   └── parser/                 # Unit tests for Tokenizer, AST nodes, and Recursive-Descent Parser
│       ├── lexer_test.cpp      # 22 tests for lexical analysis
│       ├── ast_test.cpp        # 14 tests for AST nodes & debug printing
│       └── parser_test.cpp     # 28 tests for syntax analysis & operator precedence
├── benchmarks/                 # Performance and latency benchmarks
├── data/                       # Directory for database files (.db, .wal)
└── docs/                       # Architecture diagrams and technical documentation
    └── architecture.md         # In-depth architectural breakdown
```

---

## Build Instructions

### Prerequisites
- C++20-compliant compiler (`g++`, `clang++`, or `cl.exe`)
- CMake 3.20 or later
- Build tool (`ninja` or `make`)

### 1. Standard Build

```bash
# Clone the repository
git clone https://github.com/<your-username>/QueryCraft.git
cd QueryCraft

# Configure CMake
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# Build targets (Shell binary, libraries, and tests)
cmake --build build
```

> **Note for MinGW / Make users:** You can also use `-G "MinGW Makefiles"` or `-G "Unix Makefiles"`.

### 2. Development Build with AddressSanitizer

To build with AddressSanitizer (ASan) enabled for detecting memory corruption, leaks, and buffer overflows:

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
```

---

## Run Instructions

Run the compiled interactive CLI shell:

```bash
# On Linux / macOS
./build/querycraft

# On Windows
.\build\querycraft.exe
```

### Interactive Shell Usage

```
======================================================================
  QueryCraft Database Engine (v0.1.0-alpha)
  A Custom Relational Database Engine Built from Scratch in C++20.
  Type 'HELP' for information or 'EXIT' / 'QUIT' to terminate session.
======================================================================
querycraft> HELP

--- QueryCraft Interactive Shell Help ---
Available commands:
  HELP, .help          Display this help message.
  EXIT, QUIT           Exit the interactive shell.
  .exit, .quit         Alternative exit command.

Status:
  Current Phase        Task Foundation (Phase 1)
  Storage Engine       Pending (Upcoming Phase)
  Buffer Pool Manager  Pending (Upcoming Phase)
  SQL Parser           Pending (Upcoming Phase)
  Execution Engine     Pending (Upcoming Phase)

querycraft> SELECT * FROM users;
Entered command: SELECT * FROM users;
[Note: SQL execution engine is not yet connected (Foundation phase).]
querycraft> EXIT
Exiting QueryCraft. Goodbye!
```

---

## Test Instructions

Run the unit test suite with GoogleTest:

### Direct Test Binary Execution

```bash
# On Linux / macOS
./build/querycraft_tests

# On Windows
.\build\querycraft_tests.exe
```

### Via CTest

```bash
ctest --test-dir build --output-on-failure
```

Expected output:
```
[==========] Running 80 tests from 5 test suites.
[  PASSED  ] 80 tests.
```

---

## Current Implementation Status

| Component | Status | Details |
| :--- | :--- | :--- |
| **Build System** | Completed | CMake C++20 configuration with compiler warnings, ASan toggle, and GoogleTest. |
| **Interactive Shell** | Completed | Clean REPL shell handling input echoing, graceful empty input, `HELP`, `EXIT`, and `QUIT`. |
| **Common Utilities** | Completed | Core types, engine parameters (`PAGE_SIZE = 4096`), and `Status` error reporting class. |
| **SQL Lexer (Tokenizer)** | Completed | Complete tokenizer from scratch in C++20 supporting 34 keywords (case-insensitive), identifiers, integer/float/string literals, operators, comments (`--`), and syntax error reporting. |
| **SQL Parser & AST** | Completed | Recursive-descent SQL parser supporting SELECT (*, projections, WHERE, ORDER BY, LIMIT), CREATE TABLE (with primary keys and data types), INSERT, UPDATE, DELETE, CREATE INDEX, expressions with operator precedence, and AST debug-tree printing. |
| **Unit Test Suite** | Completed | 80 unit tests covering Shell, Status, Lexer tokenization, AST representations, and full recursive-descent parser grammar. |
| **Disk Storage Manager** | Pending | Slotted-page architecture, block read/write. |
| **Buffer Pool Manager** | Pending | Frame table, dirty tracking, LRU-K page eviction. |
| **B+ Tree Index** | Pending | Dynamic balanced indexing for point queries and range scans. |
| **Execution Engine** | Pending | Volcano iterator model query execution. |

---

## License

This project is licensed under the MIT License.
