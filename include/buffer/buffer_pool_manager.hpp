#pragma once

#include "buffer/lru_replacer.hpp"
#include "common/config.hpp"
#include "common/status.hpp"
#include "storage/disk_manager.hpp"
#include "storage/page.hpp"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace querycraft::buffer {

struct Frame {
    storage::Page page{};
    page_id_t page_id{INVALID_PAGE_ID};
    int pin_count{0};
    bool is_dirty{false};
    bool is_occupied{false};

    void reset() {
        page.reset();
        page_id = INVALID_PAGE_ID;
        pin_count = 0;
        is_dirty = false;
        is_occupied = false;
    }
};

class BufferPoolManager {
public:
    BufferPoolManager(std::size_t pool_size, storage::DiskManager& disk_manager);
    ~BufferPoolManager();

    BufferPoolManager(const BufferPoolManager&) = delete;
    BufferPoolManager& operator=(const BufferPoolManager&) = delete;

    BufferPoolManager(BufferPoolManager&&) noexcept = default;
    BufferPoolManager& operator=(BufferPoolManager&&) noexcept = default;

    // Buffer Pool Operations
    storage::Page* FetchPage(page_id_t page_id);
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    storage::Page* NewPage(page_id_t& out_page_id,
                           storage::PageType type = storage::PageType::TABLE_DATA_PAGE);
    bool DeletePage(page_id_t page_id);
    bool FlushPage(page_id_t page_id);
    bool FlushAllPages();

    // Inspection & Telemetry
    [[nodiscard]] std::size_t pool_size() const noexcept { return pool_size_; }
    [[nodiscard]] storage::DiskManager& disk_manager() noexcept { return disk_manager_; }
    [[nodiscard]] int get_pin_count(page_id_t page_id) const;
    [[nodiscard]] bool is_dirty(page_id_t page_id) const;
    [[nodiscard]] bool is_cached(page_id_t page_id) const;
    [[nodiscard]] std::size_t num_free_frames() const noexcept { return free_list_.size(); }
    [[nodiscard]] std::size_t num_evictable_frames() const noexcept;

    // I/O & Cache Performance Counters
    [[nodiscard]] std::size_t num_reads() const noexcept { return num_reads_; }
    [[nodiscard]] std::size_t num_writes() const noexcept { return num_writes_; }
    [[nodiscard]] std::size_t num_hits() const noexcept { return num_hits_; }
    [[nodiscard]] std::size_t num_misses() const noexcept { return num_misses_; }
    void reset_stats() noexcept {
        num_reads_ = 0;
        num_writes_ = 0;
        num_hits_ = 0;
        num_misses_ = 0;
    }

private:
    // Helper to find a free frame or evict an eligible one
    bool find_available_frame(frame_id_t& out_frame_id);

    std::size_t pool_size_{0};
    storage::DiskManager& disk_manager_;
    std::vector<Frame> frames_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::vector<frame_id_t> free_list_;
    std::unique_ptr<LRUReplacer> replacer_;

    // Telemetry counters
    std::size_t num_reads_{0};
    std::size_t num_writes_{0};
    std::size_t num_hits_{0};
    std::size_t num_misses_{0};
};

} // namespace querycraft::buffer
