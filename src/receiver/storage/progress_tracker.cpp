#include "rxtech/progress_tracker.h"

namespace rxtech
{

    void ProgressTracker::advance(CpiContext &ctx, std::uint16_t prt, std::uint16_t channel, bool saw_tail) const noexcept
    {
        const auto ch_count = static_cast<std::uint16_t>(spec_.channels_per_prt);
        const auto pkt_per_ch = static_cast<std::uint16_t>(spec_.packets_per_channel);
        if (prt >= kCpiPrtMax || channel >= ch_count || channel >= kCpiMaxChannelCount)
        {
            return;
        }

        PrtSummary &summary = ctx.prt_summary[prt];
        if (summary.ch_recv_count[channel] == pkt_per_ch)
        {
            ++summary.ready_channel_count;
        }
        if (summary.ready_channel_count == ch_count && (summary.flags & PrtFlagComplete) == 0U)
        {
            summary.flags |= PrtFlagComplete;
            ++ctx.header.ready_prt_count;
        }
        if (saw_tail && (summary.flags & PrtFlagSeenTail) == 0U)
        {
            summary.flags |= PrtFlagSeenTail;
            ctx.header.trigger_bits |= TriggerTailObserved;
            // T-005: tail on last expected PRT → wave end
            if (ctx.header.expected_n_prt > 0U &&
                prt + 1U >= ctx.header.expected_n_prt)
            {
                ctx.header.trigger_bits |= TriggerWaveEnd;
            }
        }
        if (ctx.header.expected_slot_count > 0U && ctx.header.received_slot_count >= ctx.header.expected_slot_count)
        {
            ctx.header.trigger_bits |= TriggerFullReady;
        }
    }

} // namespace rxtech
