#pragma once

#include "common/config.hpp"
#include "common/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace querycraft::storage {

// Record identifier: identifies a record by its Page ID and Slot Index
struct RecordId {
    page_id_t page_id{INVALID_PAGE_ID};
    std::uint32_t slot_id{0};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return page_id != INVALID_PAGE_ID;
    }

    constexpr bool operator==(const RecordId& other) const noexcept = default;
};

// Page Types
enum class PageType : std::uint16_t {
    FREE_PAGE = 0,
    CATALOG_PAGE = 1,
    TABLE_DATA_PAGE = 2,
    INDEX_PAGE = 3
};

// Fixed Header layout constants (20 bytes total)
// [0..3]:   page_id (int32_t)
// [4..5]:   page_type (uint16_t)
// [6..7]:   num_slots (uint16_t)
// [8..9]:   free_space_pointer (uint16_t)
// [10..13]: prev_page_id (int32_t)
// [14..17]: next_page_id (int32_t)
// [18..19]: flags (uint16_t)
inline constexpr std::size_t PAGE_HEADER_SIZE = 20;

// Slot directory entry: [offset: uint16_t, length: uint16_t] (4 bytes)
inline constexpr std::size_t SLOT_SIZE = 4;
inline constexpr std::uint16_t DELETED_SLOT_OFFSET = 0;
inline constexpr std::uint16_t DELETED_SLOT_LENGTH = 0;

struct Slot {
    std::uint16_t offset{0};
    std::uint16_t length{0};

    [[nodiscard]] bool is_deleted() const noexcept {
        return offset == DELETED_SLOT_OFFSET || length == DELETED_SLOT_LENGTH;
    }
};

class Page {
public:
    Page();
    explicit Page(page_id_t page_id, PageType type = PageType::TABLE_DATA_PAGE);

    // Disable raw pointer copy mistakes; allow value copy/move of the 4KB buffer
    Page(const Page&) = default;
    Page& operator=(const Page&) = default;
    Page(Page&&) noexcept = default;
    Page& operator=(Page&&) noexcept = default;

    // Reset page to empty state with given page_id and type
    void reset(page_id_t page_id = INVALID_PAGE_ID, PageType type = PageType::TABLE_DATA_PAGE);

    // Raw byte buffer access
    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_.data(); }
    [[nodiscard]] std::uint8_t* data() noexcept { return data_.data(); }
    [[nodiscard]] static constexpr std::size_t size() noexcept { return PAGE_SIZE; }

    // Safe boundary-checked byte read/write
    common::Status read_bytes(std::size_t offset, std::span<std::uint8_t> dest) const;
    common::Status write_bytes(std::size_t offset, std::span<const std::uint8_t> src);

    // Header properties
    [[nodiscard]] page_id_t page_id() const noexcept;
    void set_page_id(page_id_t page_id) noexcept;

    [[nodiscard]] PageType page_type() const noexcept;
    void set_page_type(PageType type) noexcept;

    [[nodiscard]] std::uint16_t num_slots() const noexcept;
    [[nodiscard]] std::uint16_t free_space_pointer() const noexcept;

    [[nodiscard]] page_id_t prev_page_id() const noexcept;
    void set_prev_page_id(page_id_t prev_id) noexcept;

    [[nodiscard]] page_id_t next_page_id() const noexcept;
    void set_next_page_id(page_id_t next_id) noexcept;

    // Slotted-page free space calculation
    [[nodiscard]] std::size_t free_space_remaining() const noexcept;

    // Slotted Page Operations
    common::Status insert_record(std::span<const std::uint8_t> record_data, std::uint32_t& out_slot_id);
    common::Status get_record(std::uint32_t slot_id, std::vector<std::uint8_t>& out_data) const;
    common::Status update_record(std::uint32_t slot_id, std::span<const std::uint8_t> new_data);
    common::Status delete_record(std::uint32_t slot_id);

    // Slot directory queries
    [[nodiscard]] bool is_slot_deleted(std::uint32_t slot_id) const;
    [[nodiscard]] Slot get_slot(std::uint32_t slot_id) const;

    // Compacts page storage by removing fragmentation from deleted records
    void compact();

private:
    void set_num_slots(std::uint16_t count) noexcept;
    void set_free_space_pointer(std::uint16_t ptr) noexcept;
    void set_slot(std::uint32_t slot_id, Slot slot);

    std::array<std::uint8_t, PAGE_SIZE> data_{};
};

} // namespace querycraft::storage
