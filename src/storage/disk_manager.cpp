#include "storage/disk_manager.hpp"

#include <cstring>
#include <system_error>

namespace querycraft::storage {

DiskManager::DiskManager() = default;

DiskManager::DiskManager(const std::filesystem::path& db_file_path) {
    (void)open(db_file_path);
}

DiskManager::~DiskManager() {
    close();
}

DiskManager::DiskManager(DiskManager&& other) noexcept
    : file_path_(std::move(other.file_path_)),
      file_stream_(std::move(other.file_stream_)),
      num_pages_(other.num_pages_) {
    other.num_pages_ = 0;
}

DiskManager& DiskManager::operator=(DiskManager&& other) noexcept {
    if (this != &other) {
        close();
        file_path_ = std::move(other.file_path_);
        file_stream_ = std::move(other.file_stream_);
        num_pages_ = other.num_pages_;
        other.num_pages_ = 0;
    }
    return *this;
}

common::Status DiskManager::open(const std::filesystem::path& db_file_path) {
    close();
    file_path_ = db_file_path;

    std::error_code ec;
    const auto parent = db_file_path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        if (!std::filesystem::create_directories(parent, ec)) {
            return common::Status::IoError("Failed to create database directory: " + parent.string() + " (" + ec.message() + ")");
        }
    }

    // If file does not exist yet, touch it to create
    if (!std::filesystem::exists(db_file_path, ec)) {
        std::ofstream create_file(db_file_path, std::ios::out | std::ios::binary);
        if (!create_file.is_open()) {
            return common::Status::IoError("Failed to create database file: " + db_file_path.string());
        }
        create_file.close();
    }

    file_stream_.open(db_file_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_stream_.is_open()) {
        return common::Status::IoError("Failed to open database file: " + db_file_path.string());
    }

    file_stream_.seekg(0, std::ios::end);
    const auto file_size = file_stream_.tellg();
    if (file_size < 0) {
        num_pages_ = 0;
    } else {
        num_pages_ = static_cast<std::size_t>(file_size) / PAGE_SIZE;
    }

    return common::Status::Ok();
}

void DiskManager::close() {
    if (file_stream_.is_open()) {
        file_stream_.flush();
        file_stream_.close();
    }
    num_pages_ = 0;
}

bool DiskManager::is_open() const noexcept {
    return file_stream_.is_open();
}

common::Status DiskManager::allocate_page(page_id_t& out_page_id) {
    if (!is_open()) {
        return common::Status::IoError("DiskManager: database file is not open");
    }

    const auto new_page_id = static_cast<page_id_t>(num_pages_);
    Page blank_page(new_page_id, PageType::TABLE_DATA_PAGE);

    file_stream_.clear();
    file_stream_.seekp(static_cast<std::streamoff>(new_page_id) * static_cast<std::streamoff>(PAGE_SIZE));
    file_stream_.write(reinterpret_cast<const char*>(blank_page.data()), PAGE_SIZE);

    if (!file_stream_.good()) {
        return common::Status::IoError("DiskManager: failed to allocate and write page " + std::to_string(new_page_id));
    }

    file_stream_.flush();
    ++num_pages_;
    out_page_id = new_page_id;
    return common::Status::Ok();
}

common::Status DiskManager::read_page(page_id_t page_id, Page& out_page) {
    if (!is_open()) {
        return common::Status::IoError("DiskManager: database file is not open");
    }
    if (page_id < 0 || static_cast<std::size_t>(page_id) >= num_pages_) {
        return common::Status::InvalidArgument(
            "DiskManager: invalid page_id " + std::to_string(page_id) +
            " (allocated pages: " + std::to_string(num_pages_) + ")");
    }

    file_stream_.clear();
    file_stream_.seekg(static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(PAGE_SIZE));
    file_stream_.read(reinterpret_cast<char*>(out_page.data()), PAGE_SIZE);

    if (file_stream_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        return common::Status::IoError("DiskManager: failed to read complete 4096-byte page " + std::to_string(page_id));
    }

    return common::Status::Ok();
}

common::Status DiskManager::write_page(page_id_t page_id, const Page& page) {
    if (!is_open()) {
        return common::Status::IoError("DiskManager: database file is not open");
    }
    if (page_id < 0 || static_cast<std::size_t>(page_id) >= num_pages_) {
        return common::Status::InvalidArgument(
            "DiskManager: write out of bounds for page_id " + std::to_string(page_id) +
            " (allocated pages: " + std::to_string(num_pages_) + ")");
    }

    file_stream_.clear();
    file_stream_.seekp(static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(PAGE_SIZE));
    file_stream_.write(reinterpret_cast<const char*>(page.data()), PAGE_SIZE);

    if (!file_stream_.good()) {
        return common::Status::IoError("DiskManager: failed to write page " + std::to_string(page_id) + " to disk");
    }

    return common::Status::Ok();
}

common::Status DiskManager::flush() {
    if (!is_open()) {
        return common::Status::IoError("DiskManager: cannot flush closed file");
    }
    file_stream_.flush();
    if (!file_stream_.good()) {
        return common::Status::IoError("DiskManager: flush failed");
    }
    return common::Status::Ok();
}

} // namespace querycraft::storage
