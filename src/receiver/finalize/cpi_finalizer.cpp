#include "rxtech/cpi_finalizer.h"

#include "rxtech/time_utils.h"

namespace rxtech
{

    std::optional<CpiOutput> CpiFinalizer::try_finalize(CpiContext &ctx, std::uint32_t trigger) const
    {
        if (ctx.header.state != CpiState::ACTIVE && ctx.header.state != CpiState::DECIDING)
        {
            return std::nullopt;
        }

        CpiDecision decision = CpiDecision::DISCARD_INVALID;
        if ((trigger & TriggerFullReady) != 0U)
        {
            decision = CpiDecision::COMPLETE_OK;
        }
        else if ((trigger & (TriggerCpiSwitch | TriggerWaveEnd)) != 0U)
        {
            decision = ctx.header.ready_prt_count > 0U ? CpiDecision::INCOMPLETE_BUT_COMMITTABLE
                                                       : CpiDecision::ABNORMAL_CUTOFF_COMMIT;
        }
        else if ((trigger & (TriggerTimeout | TriggerStop)) != 0U)
        {
            return std::nullopt;
        }
        else
        {
            return std::nullopt;
        }

        ctx.header.state = CpiState::DECIDING;
        ctx.header.trigger_bits |= trigger;
        ctx.header.seal_tsc = steady_clock_now_ns();
        ctx.header.decision = decision;

        CpiOutput output;
        output.cpi_id = ctx.header.cpi_id;
        output.decision = decision;
        output.received_slot_count = ctx.header.received_slot_count;
        output.missing_slot_count =
            ctx.header.expected_slot_count > ctx.header.received_slot_count
                ? (ctx.header.expected_slot_count - ctx.header.received_slot_count)
                : 0U;
        output.duplicate_count = ctx.header.duplicate_count;
        output.ready_prt_count = ctx.header.ready_prt_count;
        output.first_rx_tsc = ctx.header.first_rx_tsc;
        output.last_rx_tsc = ctx.header.last_rx_tsc;
        output.seal_tsc = ctx.header.seal_tsc;
        output.pool_index = ctx.header.pool_index;
        output.view.payload_base = ctx.payload.data();
        output.view.slot_valid_bytes = ctx.slot_valid_bytes.data();
        output.view.prt_summary = ctx.prt_summary.data();
        output.view.n_prt = ctx.header.observed_n_prt > 0U ? ctx.header.observed_n_prt : ctx.header.expected_n_prt;
        output.view.slot_count = ctx.header.expected_slot_count > 0U
                                     ? ctx.header.expected_slot_count
                                     : static_cast<std::uint32_t>(output.view.n_prt) * kCpiSlotsPerPrt;

        ctx.header.state = CpiState::SEALED;
        ctx.header.state = CpiState::TOMBSTONE;
        return output;
    }

} // namespace rxtech
