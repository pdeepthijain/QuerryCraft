cmake_minimum_required(VERSION 3.20)

project(QueryCraft
    VERSION 0.1.0
    DESCRIPTION "A Custom Relational Database Engine Built from Scratch in C++20"
    LANGUAGES CXX
)

# Standard C++20 Configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for language servers and IDE tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build Options
option(BUILD_TESTS "Build QueryCraft test suite" ON)
option(BUILD_BENCHMARKS "Build QueryCraft benchmarks" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer for memory debugging" OFF)
option(ENABLE_WERROR "Treat compiler warnings as errors" OFF)

# Compiler Warning Configuration
function(querycraft_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
            $<$<BOOL:${ENABLE_WERROR}>:/WX>
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wnon-virtual-dtor
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            $<$<BOOL:${ENABLE_WERROR}>:-Werror>
        )
    endif()
endfunction()

# AddressSanitizer Configuration
if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    if(MSVC)
        add_compile_options(/fsanitize=address)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    else()
        message(WARNING "AddressSanitizer requested but compiler is not supported")
    endif()
endif()

# Global Include Directories
include_directories(include)

# -----------------------------------------------------------------------------
# Core Library: querycraft_common
# -----------------------------------------------------------------------------
add_library(querycraft_common STATIC
    src/common/shell.cpp
)

target_include_directories(querycraft_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

querycraft_enable_warnings(querycraft_common)

# -----------------------------------------------------------------------------
# Parser Library: querycraft_parser
# -----------------------------------------------------------------------------
add_library(querycraft_parser STATIC
    src/parser/lexer.cpp
    src/parser/ast.cpp
    src/parser/parser.cpp
)

target_include_directories(querycraft_parser PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(querycraft_parser PUBLIC
    querycraft_common
)

querycraft_enable_warnings(querycraft_parser)

# -----------------------------------------------------------------------------
# Storage Library: querycraft_storage
# -----------------------------------------------------------------------------
add_library(querycraft_storage STATIC
    src/storage/page.cpp
    src/storage/schema.cpp
    src/storage/record.cpp
    src/storage/disk_manager.cpp
    src/storage/table.cpp
    src/storage/database.cpp
)

target_include_directories(querycraft_storage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(querycraft_storage PUBLIC
    querycraft_common
)

querycraft_enable_warnings(querycraft_storage)

# -----------------------------------------------------------------------------
# Buffer Pool Library: querycraft_buffer
# -----------------------------------------------------------------------------
add_library(querycraft_buffer STATIC
    src/buffer/lru_replacer.cpp
    src/buffer/buffer_pool_manager.cpp
)

target_include_directories(querycraft_buffer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(querycraft_buffer PUBLIC
    querycraft_common
    querycraft_storage
)

querycraft_enable_warnings(querycraft_buffer)

# -----------------------------------------------------------------------------
# Index Library: querycraft_index
# -----------------------------------------------------------------------------
add_library(querycraft_index STATIC
    src/index/index_key.cpp
    src/index/bplus_tree_node.cpp
    src/index/bplus_tree.cpp
    src/index/index_manager.cpp
)

target_include_directories(querycraft_index PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(querycraft_index PUBLIC
    querycraft_common
    querycraft_storage
    querycraft_buffer
)

querycraft_enable_warnings(querycraft_index)

# -----------------------------------------------------------------------------
# Execution Library: querycraft_execution
# -----------------------------------------------------------------------------
add_library(querycraft_execution STATIC
    src/execution/execution_result.cpp
    src/execution/expression_evaluator.cpp
    src/execution/create_table_executor.cpp
    src/execution/insert_executor.cpp
    src/execution/select_executor.cpp
    src/execution/update_executor.cpp
    src/execution/delete_executor.cpp
    src/execution/create_index_executor.cpp
    src/execution/execution_engine.cpp
)

target_include_directories(querycraft_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(querycraft_execution PUBLIC
    querycraft_common
    querycraft_parser
    querycraft_storage
    querycraft_buffer
    querycraft_index
)

querycraft_enable_warnings(querycraft_execution)

# -----------------------------------------------------------------------------
# Main Executable: querycraft (Interactive CLI Shell)
# -----------------------------------------------------------------------------
add_executable(querycraft
    src/main.cpp
)

target_link_libraries(querycraft PRIVATE
    querycraft_execution
    querycraft_index
    querycraft_buffer
    querycraft_storage
    querycraft_parser
    querycraft_common
)

querycraft_enable_warnings(querycraft)

# -----------------------------------------------------------------------------
# Testing Target: querycraft_tests
# -----------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    include(FetchContent)

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    # Use vendored/cached source if present, otherwise download from GitHub
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.tools/googletest-1.14.0/CMakeLists.txt")
        FetchContent_Declare(
            googletest
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.tools/googletest-1.14.0"
        )
    else()
        FetchContent_Declare(
            googletest
            URL "https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip"
        )
    endif()

    FetchContent_MakeAvailable(googletest)

    add_executable(querycraft_tests
        tests/common/status_test.cpp
        tests/common/shell_test.cpp
        tests/parser/lexer_test.cpp
        tests/parser/ast_test.cpp
        tests/parser/parser_test.cpp
        tests/storage/page_test.cpp
        tests/storage/record_test.cpp
        tests/storage/disk_manager_test.cpp
        tests/storage/database_test.cpp
        tests/buffer/lru_replacer_test.cpp
        tests/buffer/buffer_pool_manager_test.cpp
        tests/index/bplus_tree_test.cpp
        tests/index/index_manager_test.cpp
        tests/execution/expression_evaluator_test.cpp
        tests/execution/execution_engine_test.cpp
        tests/execution/shell_execution_test.cpp
    )

    target_include_directories(querycraft_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(querycraft_tests PRIVATE
        querycraft_execution
        querycraft_index
        querycraft_buffer
        querycraft_storage
        querycraft_parser
        querycraft_common
        GTest::gtest_main
    )

    querycraft_enable_warnings(querycraft_tests)

    include(GoogleTest)
    gtest_discover_tests(querycraft_tests)
endif()
