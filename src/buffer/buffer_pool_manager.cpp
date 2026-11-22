#include "buffer/buffer_pool_manager.hpp"

namespace querycraft::buffer {

BufferPoolManager::BufferPoolManager(std::size_t pool_size, storage::DiskManager& disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
    frames_.resize(pool_size_);
    replacer_ = std::make_unique<LRUReplacer>(pool_size_);

    free_list_.reserve(pool_size_);
    for (std::size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
}

bool BufferPoolManager::find_available_frame(frame_id_t& out_frame_id) {
    if (!free_list_.empty()) {
        out_frame_id = free_list_.back();
        free_list_.pop_back();
        return true;
    }

    frame_id_t victim_fid = INVALID_FRAME_ID;
    if (!replacer_->Victim(&victim_fid)) {
        out_frame_id = INVALID_FRAME_ID;
        return false;
    }

    auto& frame = frames_[static_cast<std::size_t>(victim_fid)];

    // If the evicted page is dirty, flush it to disk first
    if (frame.is_dirty) {
        const auto write_st = disk_manager_.write_page(frame.page_id, frame.page);
        if (!write_st.ok()) {
            // Write failed: restore frame into replacer and abort eviction
            replacer_->Unpin(victim_fid);
            out_frame_id = INVALID_FRAME_ID;
            return false;
        }
        num_writes_++;
        frame.is_dirty = false;
    }

    page_table_.erase(frame.page_id);
    frame.reset();

    out_frame_id = victim_fid;
    return true;
}

storage::Page* BufferPoolManager::FetchPage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) {
        return nullptr;
    }

    // 1. If page is already in buffer pool (cache hit)
    const auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        const auto frame_id = it->second;
        auto& frame = frames_[static_cast<std::size_t>(frame_id)];
        frame.pin_count++;
        replacer_->Pin(frame_id);
        num_hits_++;
        return &frame.page;
    }

    // 2. Cache miss: find an available frame
    num_misses_++;
    frame_id_t frame_id = INVALID_FRAME_ID;
    if (!find_available_frame(frame_id)) {
        return nullptr; // No frame available (all frames pinned or dirty flush failed)
    }

    auto& frame = frames_[static_cast<std::size_t>(frame_id)];

    // 3. Read page from disk
    const auto read_st = disk_manager_.read_page(page_id, frame.page);
    if (!read_st.ok()) {
        frame.reset();
        free_list_.push_back(frame_id);
        return nullptr;
    }
    num_reads_++;

    // 4. Update frame metadata and page table
    frame.page_id = page_id;
    frame.pin_count = 1;
    frame.is_dirty = false;
    frame.is_occupied = true;

    page_table_[page_id] = frame_id;
    replacer_->Pin(frame_id);

    return &frame.page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    if (page_id == INVALID_PAGE_ID) {
        return false;
    }

    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    const auto frame_id = it->second;
    auto& frame = frames_[static_cast<std::size_t>(frame_id)];

    if (frame.pin_count <= 0) {
        return false;
    }

    if (is_dirty) {
        frame.is_dirty = true;
    }

    frame.pin_count--;
    if (frame.pin_count == 0) {
        replacer_->Unpin(frame_id);
    }

    return true;
}

storage::Page* BufferPoolManager::NewPage(page_id_t& out_page_id, storage::PageType type) {
    // 1. Find an available frame
    frame_id_t frame_id = INVALID_FRAME_ID;
    if (!find_available_frame(frame_id)) {
        out_page_id = INVALID_PAGE_ID;
        return nullptr;
    }

    // 2. Allocate a new page on disk
    page_id_t new_pid = INVALID_PAGE_ID;
    const auto alloc_st = disk_manager_.allocate_page(new_pid);
    if (!alloc_st.ok()) {
        free_list_.push_back(frame_id);
        out_page_id = INVALID_PAGE_ID;
        return nullptr;
    }

    // 3. Setup new frame
    auto& frame = frames_[static_cast<std::size_t>(frame_id)];
    frame.page.reset(new_pid, type);
    frame.page_id = new_pid;
    frame.pin_count = 1;
    frame.is_dirty = false;
    frame.is_occupied = true;

    page_table_[new_pid] = frame_id;
    replacer_->Pin(frame_id);

    out_page_id = new_pid;
    return &frame.page;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) {
        return true;
    }

    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return true; // Page is not in buffer pool
    }

    const auto frame_id = it->second;
    auto& frame = frames_[static_cast<std::size_t>(frame_id)];

    if (frame.pin_count > 0) {
        return false; // Pinned page cannot be deleted
    }

    page_table_.erase(it);
    replacer_->Pin(frame_id); // Remove from replacer tracking
    frame.reset();
    free_list_.push_back(frame_id);

    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) {
        return false;
    }

    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    const auto frame_id = it->second;
    auto& frame = frames_[static_cast<std::size_t>(frame_id)];

    const auto st = disk_manager_.write_page(page_id, frame.page);
    if (!st.ok()) {
        return false;
    }

    num_writes_++;
    frame.is_dirty = false;
    const auto flush_st = disk_manager_.flush();
    return flush_st.ok();
}

bool BufferPoolManager::FlushAllPages() {
    bool all_succeeded = true;

    for (auto& frame : frames_) {
        if (frame.is_occupied && frame.is_dirty) {
            const auto st = disk_manager_.write_page(frame.page_id, frame.page);
            if (st.ok()) {
                num_writes_++;
                frame.is_dirty = false;
            } else {
                all_succeeded = false;
            }
        }
    }

    const auto flush_st = disk_manager_.flush();
    if (!flush_st.ok()) {
        all_succeeded = false;
    }
    return all_succeeded;
}

int BufferPoolManager::get_pin_count(page_id_t page_id) const {
    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return -1;
    }
    return frames_[static_cast<std::size_t>(it->second)].pin_count;
}

bool BufferPoolManager::is_dirty(page_id_t page_id) const {
    const auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }
    return frames_[static_cast<std::size_t>(it->second)].is_dirty;
}

bool BufferPoolManager::is_cached(page_id_t page_id) const {
    return page_table_.find(page_id) != page_table_.end();
}

std::size_t BufferPoolManager::num_evictable_frames() const noexcept {
    return replacer_->Size();
}

} // namespace querycraft::buffer
