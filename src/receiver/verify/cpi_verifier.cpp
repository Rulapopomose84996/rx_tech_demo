#include "rxtech/cpi_verifier.h"

#include <sstream>

namespace rxtech
{

    CpiVerificationResult CpiVerifier::verify(const CpiOutput &output,
                                              const ProtocolSpec &spec) const noexcept
    {
        CpiVerificationResult result;
        result.missing_slot_count = output.missing_slot_count;
        result.duplicate_count = output.duplicate_count;

        std::ostringstream reasons;
        bool first_reason = true;

        auto add_reason = [&](const char *msg)
        {
            if (!first_reason)
                reasons << "; ";
            reasons << msg;
            first_reason = false;
        };

        // ── Check 1: finalize decision ────────────────────────────────────────
        if (output.decision == CpiDecision::ABNORMAL_CUTOFF_COMMIT)
        {
            result.error_flags |= CpiVerifyError::kAbnormalFinalize;
            add_reason("decision is ABNORMAL_CUTOFF_COMMIT");
        }
        else if (output.decision == CpiDecision::INCOMPLETE_BUT_COMMITTABLE)
        {
            result.error_flags |= CpiVerifyError::kPrematureFinalize;
            add_reason("decision is INCOMPLETE_BUT_COMMITTABLE (premature finalize)");
        }
        else if (output.decision != CpiDecision::COMPLETE_OK)
        {
            result.error_flags |= CpiVerifyError::kAbnormalFinalize;
            add_reason("decision is not COMPLETE_OK");
        }

        // ── Check 2: no missing slots ─────────────────────────────────────────
        if (output.missing_slot_count > 0U)
        {
            result.error_flags |= CpiVerifyError::kMissingPacket;
            add_reason("missing_slot_count > 0");
        }

        // ── Check 3: no duplicates ────────────────────────────────────────────
        if (output.duplicate_count > 0U)
        {
            result.error_flags |= CpiVerifyError::kDuplicatePacket;
            add_reason("duplicate_count > 0");
        }

        // ── Check 4: PRT count matches expectation ────────────────────────────
        const std::uint32_t expected_prt_count =
            output.control.valid && output.control.n_prt > 0U
                ? output.control.n_prt
                : output.view.n_prt;
        if (output.ready_prt_count < expected_prt_count)
        {
            result.missing_prt_count = expected_prt_count - output.ready_prt_count;
            result.error_flags |= CpiVerifyError::kMissingPrt;
            add_reason("ready_prt_count < expected n_prt");
        }

        // ── Checks 5 & 6: per-PRT channel coverage + per-channel packet count ─
        if (output.view.prt_summary != nullptr && expected_prt_count > 0U)
        {
            const std::uint16_t expected_ch =
                output.control.channel_count > 0U
                    ? output.control.channel_count
                    : static_cast<std::uint16_t>(spec.channels_per_prt);
            const std::uint16_t expected_pkt =
                output.control.packets_per_channel > 0U
                    ? output.control.packets_per_channel
                    : static_cast<std::uint16_t>(spec.packets_per_channel);

            for (std::uint32_t prt_idx = 0; prt_idx < expected_prt_count; ++prt_idx)
            {
                const auto &prt = output.view.prt_summary[prt_idx];

                // Channel coverage
                if (prt.ready_channel_count < expected_ch)
                {
                    ++result.bad_channel_prt_count;
                    if (!has_error(result.error_flags, CpiVerifyError::kChannelCoverage))
                    {
                        result.error_flags |= CpiVerifyError::kChannelCoverage;
                        add_reason("one or more PRTs have incomplete channel coverage");
                    }
                }

                // Per-channel packet count
                if (expected_pkt > 0U)
                {
                    for (std::uint16_t ch = 0; ch < expected_ch && ch < kCpiMaxChannelCount; ++ch)
                    {
                        if (prt.ch_recv_count[ch] < expected_pkt)
                        {
                            if (!has_error(result.error_flags, CpiVerifyError::kMissingPacket))
                            {
                                result.error_flags |= CpiVerifyError::kMissingPacket;
                                add_reason("one or more channels have fewer packets than expected");
                            }
                        }
                    }
                }
            }
        }

        result.passed = (result.error_flags == CpiVerifyError::kNone);
        if (!result.passed)
            result.reason_text = reasons.str();

        return result;
    }

} // namespace rxtech
