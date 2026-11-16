#pragma once

#include "common/config.hpp"
#include "common/status.hpp"
#include "storage/page.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace querycraft::storage {

class DiskManager {
public:
    DiskManager();
    explicit DiskManager(const std::filesystem::path& db_file_path);
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    DiskManager(DiskManager&& other) noexcept;
    DiskManager& operator=(DiskManager&& other) noexcept;

    // Open existing or create new database file
    common::Status open(const std::filesystem::path& db_file_path);

    // Safely close the database file
    void close();

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept { return file_path_; }
    [[nodiscard]] std::size_t num_pages() const noexcept { return num_pages_; }

    // Allocate a new page on disk and return its assigned page_id
    common::Status allocate_page(page_id_t& out_page_id);

    // Read a fixed 4KB page from disk by page_id
    common::Status read_page(page_id_t page_id, Page& out_page);

    // Write a fixed 4KB page to disk by page_id
    common::Status write_page(page_id_t page_id, const Page& page);

    // Flush file buffers to disk
    common::Status flush();

private:
    std::filesystem::path file_path_;
    std::fstream file_stream_;
    std::size_t num_pages_{0};
};

} // namespace querycraft::storage
