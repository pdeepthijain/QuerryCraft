#pragma once

#include "buffer/buffer_pool_manager.hpp"
#include "index/index_manager.hpp"
#include "storage/database.hpp"

namespace querycraft::execution {

class ExecutionContext {
public:
    explicit ExecutionContext(storage::Database& database,
                             index::IndexManager* index_manager = nullptr,
                             buffer::BufferPoolManager* buffer_pool_manager = nullptr)
        : database_(database),
          index_manager_(index_manager),
          buffer_pool_manager_(buffer_pool_manager) {}

    [[nodiscard]] storage::Database& database() noexcept { return database_; }
    [[nodiscard]] const storage::Database& database() const noexcept { return database_; }

    [[nodiscard]] index::IndexManager* index_manager() noexcept { return index_manager_; }
    [[nodiscard]] const index::IndexManager* index_manager() const noexcept { return index_manager_; }

    [[nodiscard]] buffer::BufferPoolManager* buffer_pool_manager() noexcept { return buffer_pool_manager_; }
    [[nodiscard]] const buffer::BufferPoolManager* buffer_pool_manager() const noexcept { return buffer_pool_manager_; }

private:
    storage::Database& database_;
    index::IndexManager* index_manager_{nullptr};
    buffer::BufferPoolManager* buffer_pool_manager_{nullptr};
};

} // namespace querycraft::execution
