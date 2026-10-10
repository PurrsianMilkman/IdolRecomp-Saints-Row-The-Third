// sr3physics/job_dispatch.h implementation. spec-physics-runtime.md Sec9
// (unit W-U0) - see the header's own doc comments for citations.
#include "sr3physics/job_dispatch.h"

#include <stdexcept>

namespace sr3physics {

void JobTypeTable::registerJobType(uint8_t type, std::vector<JobTypeRow::Handler> subtypeTable,
                                    bool hasCustomProcFunc, bool hasCustomFinishFunc) {
    // "The whole job-type table is built exactly once, at physics bring-up
    // ... and never touched again anywhere else in the binary" (Sec9.1,
    // CONFIRMED). seal()'s own doc comment explains why this throws rather
    // than silently allowing a second write.
    if (sealed_) {
        throw std::logic_error(
            "sr3physics::JobTypeTable::registerJobType called after seal() "
            "(spec-physics-runtime.md Sec9.1: the job-type table is built exactly "
            "once, at physics bring-up, and never touched again)");
    }
    rows_[type].registerSubtypes(std::move(subtypeTable), hasCustomProcFunc, hasCustomFinishFunc);
}

const JobTypeRow& JobTypeTable::rowFor(uint8_t type) const {
    auto it = rows_.find(type);
    if (it != rows_.end()) return it->second;
    // The constructor's own bootstrap default (0x00be8960, Sec9.2,
    // CONFIRMED) for any type never passed to registerJobType(): NULL
    // table, zero length, both side-funcs still the default stub - which
    // is exactly JobTypeRow's own default-constructed state.
    static const JobTypeRow kDefaultRow;
    return kDefaultRow;
}

void JobTypeTable::dispatch(const JobDescriptor& desc) const {
    const JobTypeRow& row = rowFor(desc.type);
    const JobTypeRow::Handler& handler = row.handlerFor(desc.subtype);
    if (!handler) throw JobDispatchFault(desc.type, desc.subtype);
    handler(desc);
}

size_t JobQueueFifo::drainAll(const JobTypeTable& table) {
    size_t n = 0;
    while (!pending_.empty()) {
        JobDescriptor desc = pending_.front();
        pending_.pop_front();
        table.dispatch(desc);
        ++n;
    }
    return n;
}

}  // namespace sr3physics
