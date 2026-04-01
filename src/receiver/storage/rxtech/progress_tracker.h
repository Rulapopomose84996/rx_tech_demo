#pragma once

#include "rxtech/cpi_context.h"
#include "rxtech/slot_writer.h"

namespace rxtech {

struct ProgressUpdate {
    bool full_ready = false;
    bool duplicate = false;
    std::uint32_t missing_fragments = 0;
};

class ProgressTracker {
public:
    ProgressUpdate on_write(const CpiContext& context, const SlotWriteResult& write_result) const;
};

}  // namespace rxtech
