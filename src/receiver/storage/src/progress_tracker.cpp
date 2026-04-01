#include "rxtech/progress_tracker.h"

namespace rxtech {

ProgressUpdate ProgressTracker::on_write(const CpiContext& context, const SlotWriteResult& write_result) const {
    ProgressUpdate update;
    update.duplicate = write_result.duplicate;
    if (context.expected_frag_count >= context.received_fragments) {
        update.missing_fragments = static_cast<std::uint32_t>(context.expected_frag_count - context.received_fragments);
    }
    update.full_ready = context.expected_frag_count != 0U &&
                        context.received_fragments == context.expected_frag_count &&
                        context.received_payload_bytes == context.expected_block_bytes;
    return update;
}

}  // namespace rxtech
