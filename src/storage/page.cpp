#include "storage/page.hpp"

#include <algorithm>
#include <cstring>

namespace querycraft::storage {

namespace {

// Helper to safely read a 16-bit unsigned integer in little-endian order
inline std::uint16_t read_uint16(const std::uint8_t* ptr) noexcept {
    std::uint16_t val = 0;
    std::memcpy(&val, ptr, sizeof(val));
    return val;
}

// Helper to safely write a 16-bit unsigned integer in little-endian order
inline void write_uint16(std::uint8_t* ptr, std::uint16_t val) noexcept {
    std::memcpy(ptr, &val, sizeof(val));
}

// Helper to safely read a 32-bit signed integer in little-endian order
inline std::int32_t read_int32(const std::uint8_t* ptr) noexcept {
    std::int32_t val = 0;
    std::memcpy(&val, ptr, sizeof(val));
    return val;
}

// Helper to safely write a 32-bit signed integer in little-endian order
inline void write_int32(std::uint8_t* ptr, std::int32_t val) noexcept {
    std::memcpy(ptr, &val, sizeof(val));
}

} // namespace

Page::Page() {
    reset(INVALID_PAGE_ID, PageType::TABLE_DATA_PAGE);
}

Page::Page(page_id_t page_id, PageType type) {
    reset(page_id, type);
}

void Page::reset(page_id_t page_id, PageType type) {
    data_.fill(0);
    set_page_id(page_id);
    set_page_type(type);
    set_num_slots(0);
    set_free_space_pointer(static_cast<std::uint16_t>(PAGE_SIZE));
    set_prev_page_id(INVALID_PAGE_ID);
    set_next_page_id(INVALID_PAGE_ID);
}

common::Status Page::read_bytes(std::size_t offset, std::span<std::uint8_t> dest) const {
    if (offset > PAGE_SIZE || dest.size() > PAGE_SIZE - offset) {
        return common::Status::InvalidArgument("read_bytes: offset and length exceed page boundary");
    }
    std::memcpy(dest.data(), data_.data() + offset, dest.size());
    return common::Status::Ok();
}

common::Status Page::write_bytes(std::size_t offset, std::span<const std::uint8_t> src) {
    if (offset > PAGE_SIZE || src.size() > PAGE_SIZE - offset) {
        return common::Status::InvalidArgument("write_bytes: offset and length exceed page boundary");
    }
    std::memcpy(data_.data() + offset, src.data(), src.size());
    return common::Status::Ok();
}

page_id_t Page::page_id() const noexcept {
    return read_int32(data_.data() + 0);
}

void Page::set_page_id(page_id_t page_id) noexcept {
    write_int32(data_.data() + 0, page_id);
}

PageType Page::page_type() const noexcept {
    return static_cast<PageType>(read_uint16(data_.data() + 4));
}

void Page::set_page_type(PageType type) noexcept {
    write_uint16(data_.data() + 4, static_cast<std::uint16_t>(type));
}

std::uint16_t Page::num_slots() const noexcept {
    return read_uint16(data_.data() + 6);
}

void Page::set_num_slots(std::uint16_t count) noexcept {
    write_uint16(data_.data() + 6, count);
}

std::uint16_t Page::free_space_pointer() const noexcept {
    return read_uint16(data_.data() + 8);
}

void Page::set_free_space_pointer(std::uint16_t ptr) noexcept {
    write_uint16(data_.data() + 8, ptr);
}

page_id_t Page::prev_page_id() const noexcept {
    return read_int32(data_.data() + 10);
}

void Page::set_prev_page_id(page_id_t prev_id) noexcept {
    write_int32(data_.data() + 10, prev_id);
}

page_id_t Page::next_page_id() const noexcept {
    return read_int32(data_.data() + 14);
}

void Page::set_next_page_id(page_id_t next_id) noexcept {
    write_int32(data_.data() + 14, next_id);
}

std::size_t Page::free_space_remaining() const noexcept {
    const std::size_t slot_dir_end = PAGE_HEADER_SIZE + static_cast<std::size_t>(num_slots()) * SLOT_SIZE;
    const std::size_t free_ptr = free_space_pointer();
    if (free_ptr <= slot_dir_end) {
        return 0;
    }
    return free_ptr - slot_dir_end;
}

Slot Page::get_slot(std::uint32_t slot_id) const {
    if (slot_id >= num_slots()) {
        return Slot{DELETED_SLOT_OFFSET, DELETED_SLOT_LENGTH};
    }
    const std::size_t offset = PAGE_HEADER_SIZE + slot_id * SLOT_SIZE;
    const std::uint16_t slot_offset = read_uint16(data_.data() + offset);
    const std::uint16_t slot_length = read_uint16(data_.data() + offset + 2);
    return Slot{slot_offset, slot_length};
}

void Page::set_slot(std::uint32_t slot_id, Slot slot) {
    const std::size_t offset = PAGE_HEADER_SIZE + slot_id * SLOT_SIZE;
    write_uint16(data_.data() + offset, slot.offset);
    write_uint16(data_.data() + offset + 2, slot.length);
}

bool Page::is_slot_deleted(std::uint32_t slot_id) const {
    if (slot_id >= num_slots()) {
        return true;
    }
    return get_slot(slot_id).is_deleted();
}

common::Status Page::insert_record(std::span<const std::uint8_t> record_data, std::uint32_t& out_slot_id) {
    if (record_data.size() > PAGE_SIZE - PAGE_HEADER_SIZE - SLOT_SIZE) {
        return common::Status::InvalidArgument("Record size exceeds maximum page capacity");
    }

    const auto req_size = static_cast<std::size_t>(record_data.size());

    // Check if there is an existing deleted slot that can be reused
    int reuse_slot_index = -1;
    for (std::uint32_t i = 0; i < num_slots(); ++i) {
        if (is_slot_deleted(i)) {
            reuse_slot_index = static_cast<int>(i);
            break;
        }
    }

    const std::size_t slot_overhead = (reuse_slot_index >= 0) ? 0 : SLOT_SIZE;
    const std::size_t total_needed = req_size + slot_overhead;

    // Check if free space is currently insufficient; if so, attempt compaction
    if (free_space_remaining() < total_needed) {
        compact();
        // Recalculate reusable slot index after compaction (since compact may trim trailing deleted slots)
        reuse_slot_index = -1;
        for (std::uint32_t i = 0; i < num_slots(); ++i) {
            if (is_slot_deleted(i)) {
                reuse_slot_index = static_cast<int>(i);
                break;
            }
        }
        const std::size_t post_compact_overhead = (reuse_slot_index >= 0) ? 0 : SLOT_SIZE;
        if (free_space_remaining() < req_size + post_compact_overhead) {
            return common::Status::InvalidArgument("Insufficient space on page to insert record");
        }
    }

    std::uint32_t slot_id = 0;
    if (reuse_slot_index >= 0) {
        slot_id = static_cast<std::uint32_t>(reuse_slot_index);
    } else {
        slot_id = num_slots();
        set_num_slots(static_cast<std::uint16_t>(slot_id + 1));
    }

    const auto new_free_ptr = static_cast<std::uint16_t>(free_space_pointer() - req_size);
    set_free_space_pointer(new_free_ptr);

    if (req_size > 0) {
        std::memcpy(data_.data() + new_free_ptr, record_data.data(), req_size);
    }

    set_slot(slot_id, Slot{new_free_ptr, static_cast<std::uint16_t>(req_size)});
    out_slot_id = slot_id;

    return common::Status::Ok();
}

common::Status Page::get_record(std::uint32_t slot_id, std::vector<std::uint8_t>& out_data) const {
    if (slot_id >= num_slots()) {
        return common::Status::NotFound("Slot index out of range: " + std::to_string(slot_id));
    }
    const Slot slot = get_slot(slot_id);
    if (slot.is_deleted()) {
        return common::Status::NotFound("Slot has been deleted: " + std::to_string(slot_id));
    }

    if (slot.offset < PAGE_HEADER_SIZE || static_cast<std::size_t>(slot.offset) + slot.length > PAGE_SIZE) {
        return common::Status::InternalError("Corrupted slot directory entry detected on page");
    }

    out_data.resize(slot.length);
    if (slot.length > 0) {
        std::memcpy(out_data.data(), data_.data() + slot.offset, slot.length);
    }
    return common::Status::Ok();
}

common::Status Page::update_record(std::uint32_t slot_id, std::span<const std::uint8_t> new_data) {
    if (slot_id >= num_slots()) {
        return common::Status::NotFound("Slot index out of range: " + std::to_string(slot_id));
    }
    Slot slot = get_slot(slot_id);
    if (slot.is_deleted()) {
        return common::Status::NotFound("Cannot update deleted slot: " + std::to_string(slot_id));
    }

    const auto new_len = static_cast<std::size_t>(new_data.size());

    // Case 1: In-place update if new size is smaller or equal to current allocated slot length
    if (new_len <= slot.length) {
        if (new_len > 0) {
            std::memcpy(data_.data() + slot.offset, new_data.data(), new_len);
        }
        slot.length = static_cast<std::uint16_t>(new_len);
        set_slot(slot_id, slot);
        return common::Status::Ok();
    }

    // Case 2: Record grew larger.
    // Temporarily mark slot as deleted and check if enough space exists (compacting if necessary).
    const auto old_offset = slot.offset;
    const auto old_length = slot.length;
    set_slot(slot_id, Slot{DELETED_SLOT_OFFSET, DELETED_SLOT_LENGTH});

    if (free_space_remaining() < new_len) {
        compact();
    }

    if (free_space_remaining() < new_len) {
        // Rollback slot to previous state
        set_slot(slot_id, Slot{old_offset, old_length});
        return common::Status::InvalidArgument("Insufficient space on page to update record");
    }

    const auto new_free_ptr = static_cast<std::uint16_t>(free_space_pointer() - new_len);
    set_free_space_pointer(new_free_ptr);
    if (new_len > 0) {
        std::memcpy(data_.data() + new_free_ptr, new_data.data(), new_len);
    }
    set_slot(slot_id, Slot{new_free_ptr, static_cast<std::uint16_t>(new_len)});

    return common::Status::Ok();
}

common::Status Page::delete_record(std::uint32_t slot_id) {
    if (slot_id >= num_slots()) {
        return common::Status::NotFound("Slot index out of range: " + std::to_string(slot_id));
    }
    if (is_slot_deleted(slot_id)) {
        return common::Status::NotFound("Slot is already deleted: " + std::to_string(slot_id));
    }

    set_slot(slot_id, Slot{DELETED_SLOT_OFFSET, DELETED_SLOT_LENGTH});

    // If this is the last slot in the directory, trim contiguous trailing deleted slots
    auto slots = num_slots();
    while (slots > 0 && is_slot_deleted(slots - 1)) {
        --slots;
    }
    set_num_slots(slots);

    return common::Status::Ok();
}

void Page::compact() {
    struct LiveRecord {
        std::uint32_t slot_id;
        std::vector<std::uint8_t> bytes;
    };

    std::vector<LiveRecord> live_records;
    const auto count = num_slots();
    live_records.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i) {
        const Slot slot = get_slot(i);
        if (!slot.is_deleted()) {
            std::vector<std::uint8_t> rec(slot.length);
            if (slot.length > 0 && slot.offset + slot.length <= PAGE_SIZE) {
                std::memcpy(rec.data(), data_.data() + slot.offset, slot.length);
            }
            live_records.push_back(LiveRecord{i, std::move(rec)});
        }
    }

    // Determine the end of the slot directory
    const std::size_t slot_dir_bytes = PAGE_HEADER_SIZE + static_cast<std::size_t>(count) * SLOT_SIZE;

    // Zero out everything between slot directory and end of page
    if (slot_dir_bytes < PAGE_SIZE) {
        std::memset(data_.data() + slot_dir_bytes, 0, PAGE_SIZE - slot_dir_bytes);
    }

    // Re-pack all live records from the top of the page downward
    std::size_t cur_free_ptr = PAGE_SIZE;
    for (const auto& rec : live_records) {
        cur_free_ptr -= rec.bytes.size();
        if (!rec.bytes.empty()) {
            std::memcpy(data_.data() + cur_free_ptr, rec.bytes.data(), rec.bytes.size());
        }
        set_slot(rec.slot_id, Slot{static_cast<std::uint16_t>(cur_free_ptr), static_cast<std::uint16_t>(rec.bytes.size())});
    }

    set_free_space_pointer(static_cast<std::uint16_t>(cur_free_ptr));

    // Trim any trailing deleted slots from directory
    auto updated_count = count;
    while (updated_count > 0 && is_slot_deleted(updated_count - 1)) {
        --updated_count;
    }
    set_num_slots(updated_count);
}

} // namespace querycraft::storage
