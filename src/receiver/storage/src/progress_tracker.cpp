#include "rxtech/progress_tracker.h"

namespace rxtech
{

    void ProgressTracker::advance(CpiContext &ctx, std::uint16_t prt, std::uint16_t channel, bool saw_tail) const noexcept
    {
        if (prt >= kCpiPrtMax || channel >= kCpiChannelCount)
        {
            return;
        }

        PrtSummary &summary = ctx.prt_summary[prt];
        if (summary.ch_recv_count[channel] == kCpiPacketsPerChannel)
        {
            ++summary.ready_channel_count;
        }
        if (summary.ready_channel_count == kCpiChannelCount && (summary.flags & PrtFlagComplete) == 0U)
        {
            summary.flags |= PrtFlagComplete;
            ++ctx.header.ready_prt_count;
        }
        if (saw_tail && (summary.flags & PrtFlagSeenTail) == 0U)
        {
            summary.flags |= PrtFlagSeenTail;
            ctx.header.trigger_bits |= TriggerTailObserved;
        }
        if (ctx.header.expected_slot_count > 0U && ctx.header.received_slot_count >= ctx.header.expected_slot_count)
        {
            ctx.header.trigger_bits |= TriggerFullReady;
        }
    }

} // namespace rxtech
