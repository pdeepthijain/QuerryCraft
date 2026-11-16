#include "storage/table.hpp"

namespace querycraft::storage {

Table::Table(std::string name,
             Schema schema,
             DiskManager& disk_manager,
             page_id_t first_page_id,
             page_id_t last_page_id)
    : name_(std::move(name)),
      schema_(std::move(schema)),
      disk_manager_(disk_manager),
      first_page_id_(first_page_id),
      last_page_id_(last_page_id) {}

common::Status Table::insert_record(Record& record, RecordId& out_rid) {
    const auto val_status = record.validate(schema_);
    if (!val_status.ok()) {
        return val_status;
    }

    const auto bytes = record.serialize();

    if (first_page_id_ == INVALID_PAGE_ID) {
        page_id_t new_pid = INVALID_PAGE_ID;
        const auto alloc_status = disk_manager_.allocate_page(new_pid);
        if (!alloc_status.ok()) {
            return alloc_status;
        }
        first_page_id_ = new_pid;
        last_page_id_ = new_pid;
    }

    Page page;
    auto status = disk_manager_.read_page(last_page_id_, page);
    if (!status.ok()) {
        return status;
    }

    std::uint32_t slot_id = 0;
    status = page.insert_record(bytes, slot_id);
    if (status.ok()) {
        const auto write_status = disk_manager_.write_page(last_page_id_, page);
        if (!write_status.ok()) {
            return write_status;
        }
        out_rid = RecordId{last_page_id_, slot_id};
        record.set_rid(out_rid);
        return common::Status::Ok();
    }

    // Current page is full: allocate a new chained data page
    page_id_t new_page_id = INVALID_PAGE_ID;
    status = disk_manager_.allocate_page(new_page_id);
    if (!status.ok()) {
        return status;
    }

    Page new_page(new_page_id, PageType::TABLE_DATA_PAGE);
    page.set_next_page_id(new_page_id);
    new_page.set_prev_page_id(last_page_id_);

    auto write_status = disk_manager_.write_page(last_page_id_, page);
    if (!write_status.ok()) {
        return write_status;
    }

    last_page_id_ = new_page_id;
    status = new_page.insert_record(bytes, slot_id);
    if (!status.ok()) {
        return status;
    }

    write_status = disk_manager_.write_page(new_page_id, new_page);
    if (!write_status.ok()) {
        return write_status;
    }

    out_rid = RecordId{new_page_id, slot_id};
    record.set_rid(out_rid);
    return common::Status::Ok();
}

common::Status Table::get_record(RecordId rid, Record& out_record) const {
    if (!rid.is_valid()) {
        return common::Status::InvalidArgument("Table::get_record: invalid RecordId provided");
    }

    Page page;
    auto status = disk_manager_.read_page(rid.page_id, page);
    if (!status.ok()) {
        return status;
    }

    std::vector<std::uint8_t> raw_bytes;
    status = page.get_record(rid.slot_id, raw_bytes);
    if (!status.ok()) {
        return status;
    }

    status = Record::deserialize(raw_bytes, out_record);
    if (!status.ok()) {
        return status;
    }

    out_record.set_rid(rid);
    return common::Status::Ok();
}

common::Status Table::update_record(RecordId rid, const Record& new_record) {
    if (!rid.is_valid()) {
        return common::Status::InvalidArgument("Table::update_record: invalid RecordId provided");
    }

    const auto val_status = new_record.validate(schema_);
    if (!val_status.ok()) {
        return val_status;
    }

    const auto bytes = new_record.serialize();

    Page page;
    auto status = disk_manager_.read_page(rid.page_id, page);
    if (!status.ok()) {
        return status;
    }

    status = page.update_record(rid.slot_id, bytes);
    if (!status.ok()) {
        return status;
    }

    return disk_manager_.write_page(rid.page_id, page);
}

common::Status Table::delete_record(RecordId rid) {
    if (!rid.is_valid()) {
        return common::Status::InvalidArgument("Table::delete_record: invalid RecordId provided");
    }

    Page page;
    auto status = disk_manager_.read_page(rid.page_id, page);
    if (!status.ok()) {
        return status;
    }

    status = page.delete_record(rid.slot_id);
    if (!status.ok()) {
        return status;
    }

    return disk_manager_.write_page(rid.page_id, page);
}

common::Status Table::scan_records(std::vector<Record>& out_records) const {
    out_records.clear();
    if (first_page_id_ == INVALID_PAGE_ID) {
        return common::Status::Ok();
    }

    page_id_t curr_pid = first_page_id_;
    while (curr_pid != INVALID_PAGE_ID) {
        Page page;
        auto status = disk_manager_.read_page(curr_pid, page);
        if (!status.ok()) {
            return status;
        }

        const auto count = page.num_slots();
        for (std::uint32_t slot = 0; slot < count; ++slot) {
            if (!page.is_slot_deleted(slot)) {
                std::vector<std::uint8_t> raw_bytes;
                if (page.get_record(slot, raw_bytes).ok()) {
                    Record rec;
                    if (Record::deserialize(raw_bytes, rec).ok()) {
                        rec.set_rid(RecordId{curr_pid, slot});
                        out_records.push_back(std::move(rec));
                    }
                }
            }
        }

        curr_pid = page.next_page_id();
    }

    return common::Status::Ok();
}

} // namespace querycraft::storage
