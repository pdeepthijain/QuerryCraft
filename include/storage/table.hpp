#pragma once

#include "common/config.hpp"
#include "common/status.hpp"
#include "storage/disk_manager.hpp"
#include "storage/page.hpp"
#include "storage/record.hpp"
#include "storage/schema.hpp"

#include <memory>
#include <string>
#include <vector>

namespace querycraft::storage {

class Table {
public:
    Table(std::string name,
          Schema schema,
          DiskManager& disk_manager,
          page_id_t first_page_id = INVALID_PAGE_ID,
          page_id_t last_page_id = INVALID_PAGE_ID);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const Schema& schema() const noexcept { return schema_; }
    [[nodiscard]] page_id_t first_page_id() const noexcept { return first_page_id_; }
    [[nodiscard]] page_id_t last_page_id() const noexcept { return last_page_id_; }

    void set_first_page_id(page_id_t pid) noexcept { first_page_id_ = pid; }
    void set_last_page_id(page_id_t pid) noexcept { last_page_id_ = pid; }

    // CRUD operations on table records
    common::Status insert_record(Record& record, RecordId& out_rid);
    common::Status get_record(RecordId rid, Record& out_record) const;
    common::Status update_record(RecordId rid, const Record& new_record);
    common::Status delete_record(RecordId rid);

    // Full table scan iterating through slotted data pages
    common::Status scan_records(std::vector<Record>& out_records) const;

private:
    std::string name_;
    Schema schema_;
    DiskManager& disk_manager_;
    page_id_t first_page_id_{INVALID_PAGE_ID};
    page_id_t last_page_id_{INVALID_PAGE_ID};
};

} // namespace querycraft::storage
