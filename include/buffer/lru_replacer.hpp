#pragma once

#include "common/config.hpp"

#include <cstddef>
#include <list>
#include <unordered_map>

namespace querycraft::buffer {

class LRUReplacer {
public:
    explicit LRUReplacer(std::size_t num_frames);
    ~LRUReplacer() = default;

    LRUReplacer(const LRUReplacer&) = delete;
    LRUReplacer& operator=(const LRUReplacer&) = delete;
    LRUReplacer(LRUReplacer&&) noexcept = default;
    LRUReplacer& operator=(LRUReplacer&&) noexcept = default;

    // Select the least-recently-used evictable frame
    bool Victim(frame_id_t* frame_id);

    // Remove frame from replacer (pinned, not evictable)
    void Pin(frame_id_t frame_id);

    // Add frame to replacer (unpinned, eligible for eviction)
    void Unpin(frame_id_t frame_id);

    // Record access to an evictable frame (moves to MRU position)
    void RecordAccess(frame_id_t frame_id);

    // Number of currently evictable frames
    [[nodiscard]] std::size_t Size() const noexcept;

    // Total capacity
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }

private:
    std::size_t capacity_{0};
    // Front = least recently used (LRU, next eviction candidate)
    // Back  = most recently used (MRU)
    std::list<frame_id_t> lru_list_;
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> entry_map_;
};

} // namespace querycraft::buffer
