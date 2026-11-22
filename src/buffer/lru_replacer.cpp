#include "buffer/lru_replacer.hpp"

namespace querycraft::buffer {

LRUReplacer::LRUReplacer(std::size_t num_frames) : capacity_(num_frames) {}

bool LRUReplacer::Victim(frame_id_t* frame_id) {
    if (lru_list_.empty()) {
        if (frame_id != nullptr) {
            *frame_id = INVALID_FRAME_ID;
        }
        return false;
    }

    const auto victim_fid = lru_list_.front();
    lru_list_.pop_front();
    entry_map_.erase(victim_fid);

    if (frame_id != nullptr) {
        *frame_id = victim_fid;
    }
    return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {
    const auto it = entry_map_.find(frame_id);
    if (it != entry_map_.end()) {
        lru_list_.erase(it->second);
        entry_map_.erase(it);
    }
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
    if (frame_id < 0 || static_cast<std::size_t>(frame_id) >= capacity_) {
        return;
    }

    // If frame is already in replacer, do not insert duplicate
    if (entry_map_.find(frame_id) != entry_map_.end()) {
        return;
    }

    // Push to MRU (back)
    lru_list_.push_back(frame_id);
    entry_map_[frame_id] = std::prev(lru_list_.end());
}

void LRUReplacer::RecordAccess(frame_id_t frame_id) {
    const auto it = entry_map_.find(frame_id);
    if (it != entry_map_.end()) {
        lru_list_.erase(it->second);
        lru_list_.push_back(frame_id);
        it->second = std::prev(lru_list_.end());
    }
}

std::size_t LRUReplacer::Size() const noexcept {
    return lru_list_.size();
}

} // namespace querycraft::buffer
