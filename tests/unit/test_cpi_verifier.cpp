/// Unit tests for CpiVerifier.
///
/// Test matrix:
///   1. Complete CPI — normal pass
///   2. Missing packets (missing_slot_count > 0)
///   3. Duplicate packets
///   4. Channel coverage error (ready_channel_count < expected)
///   5. Missing PRTs (ready_prt_count < n_prt)
///   6. Abnormal finalize decision (ABNORMAL_CUTOFF_COMMIT)
///   7. Premature finalize (INCOMPLETE_BUT_COMMITTABLE)
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <memory>

#include "rxtech/cpi_context.h"
#include "rxtech/cpi_context_pool.h"
#include "rxtech/cpi_finalizer.h"
#include "rxtech/cpi_verifier.h"
#include "rxtech/progress_tracker.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/slot_writer.h"

namespace
{
    rxtech::ProtocolSpec make_spec(std::uint32_t channels = 3U, std::uint32_t pkts = 9U)
    {
        rxtech::ProtocolSpec s;
        s.channels_per_prt = channels;
        s.packets_per_channel = pkts;
        return s;
    }

    // Write all packets for one PRT (all channels, all packet indices).
    void fill_prt(rxtech::CpiContext &ctx,
                  const rxtech::ProtocolSpec &spec,
                  std::uint16_t prt)
    {
        rxtech::SlotWriter writer(spec);
        rxtech::ProgressTracker tracker(spec);
        std::vector<std::uint8_t> dummy_payload(2032U, 0xABU);

        for (std::uint32_t ch = 0; ch < spec.channels_per_prt; ++ch)
        {
            for (std::uint32_t pi = 1; pi <= spec.packets_per_channel; ++pi)
            {
                rxtech::ParsedPacketView pkt;
                pkt.valid = true;
                pkt.kind = rxtech::PacketKind::data_packet;
                pkt.cpi = ctx.header.cpi_id;
                pkt.channel = static_cast<std::uint16_t>(ch);
                pkt.prt = prt;
                pkt.packet_index = static_cast<std::uint16_t>(pi);
                pkt.payload_ptr = dummy_payload.data();
                pkt.payload_len = 2032U;
                pkt.rx_tsc = 0U;

                writer.write(ctx, pkt);
                tracker.advance(ctx, prt, static_cast<std::uint16_t>(ch),
                                pi == spec.packets_per_channel);
            }
        }
    }

    // Build a fully populated CpiContext with n_prt complete PRTs.
    rxtech::CpiOutput make_complete_output(const rxtech::ProtocolSpec &spec,
                                           std::uint16_t cpi_id,
                                           std::uint16_t n_prt)
    {
        auto ctx_ptr = std::make_unique<rxtech::CpiContext>();
        rxtech::CpiContext &ctx = *ctx_ptr;
        ctx.reset(cpi_id, 0U);
        ctx.header.channels_per_prt = static_cast<std::uint16_t>(spec.channels_per_prt);
        ctx.header.packets_per_channel = static_cast<std::uint16_t>(spec.packets_per_channel);
        ctx.header.expected_n_prt = n_prt;
        ctx.header.expected_slot_count = static_cast<std::uint32_t>(n_prt) *
                                         spec.channels_per_prt *
                                         spec.packets_per_channel;
        ctx.header.state = rxtech::CpiState::ACTIVE;

        for (std::uint16_t prt = 1U; prt <= n_prt; ++prt)
        {
            fill_prt(ctx, spec, prt);
        }
        ctx.header.observed_n_prt = n_prt;

        rxtech::CpiFinalizer finalizer;
        const auto maybe = finalizer.try_finalize(ctx, rxtech::TriggerFullReady);
        assert(maybe.has_value());
        return *maybe;
    }

} // namespace

int main()
{
    const rxtech::ProtocolSpec spec = make_spec(3U, 9U);
    rxtech::CpiVerifier verifier;

    // ── Test 1: complete CPI, normal pass ─────────────────────────────────────
    {
        const auto output = make_complete_output(spec, 1U, 2U);
        const auto result = verifier.verify(output, spec);
        assert(result.passed);
        assert(result.error_flags == rxtech::CpiVerifyError::kNone);
        assert(result.reason_text.empty());
        assert(result.missing_slot_count == 0U);
        assert(result.duplicate_count == 0U);
    }

    // ── Test 2: missing packets ─────────────────────────────────────────────
    {
        // Use a complete output then manually inflate missing_slot_count.
        rxtech::CpiOutput output = make_complete_output(spec, 2U, 1U);
        output.missing_slot_count = 5U;
        // decision stays COMPLETE_OK

        const auto result = verifier.verify(output, spec);
        assert(!result.passed);
        assert(rxtech::has_error(result.error_flags, rxtech::CpiVerifyError::kMissingPacket));
        assert(result.missing_slot_count == 5U);
    }

    // ── Test 3: duplicate packets ────────────────────────────────────────────
    {
        rxtech::CpiOutput output = make_complete_output(spec, 3U, 1U);
        output.duplicate_count = 2U;

        const auto result = verifier.verify(output, spec);
        assert(!result.passed);
        assert(rxtech::has_error(result.error_flags, rxtech::CpiVerifyError::kDuplicatePacket));
        assert(result.duplicate_count == 2U);
    }

    // ── Test 4: channel coverage error ──────────────────────────────────────
    {
        rxtech::CpiOutput output = make_complete_output(spec, 4U, 2U);
        // Corrupt prt_summary[0] to simulate a missing channel
        // Note: prt_summary is a pointer into the CpiContext which is destroyed.
        // We cannot safely mutate it post-finalization.  Instead build a context
        // manually that results in a partial PRT.
        auto ctx_ptr = std::make_unique<rxtech::CpiContext>();
        rxtech::CpiContext &ctx = *ctx_ptr;
        ctx.reset(4U, 0U);
        ctx.header.channels_per_prt = 3U;
        ctx.header.packets_per_channel = 9U;
        ctx.header.expected_n_prt = 1U;
        ctx.header.expected_slot_count = 1U * 3U * 9U;
        ctx.header.state = rxtech::CpiState::ACTIVE;
        ctx.header.observed_n_prt = 1U;

        // Fill only channels 0 and 1 of PRT 1 (leave channel 2 empty)
        for (std::uint16_t ch = 0; ch < 2U; ++ch)
        {
            for (std::uint16_t pi = 1U; pi <= 9U; ++pi)
            {
                rxtech::ParsedPacketView pkt{};
                pkt.valid = true;
                pkt.kind = rxtech::PacketKind::data_packet;
                pkt.cpi = 4U;
                pkt.channel = ch;
                pkt.prt = 1U;
                pkt.packet_index = pi;
                pkt.payload_len = 2032U;
                std::vector<std::uint8_t> dummy(2032U, 0U);
                pkt.payload_ptr = dummy.data();

                rxtech::ProtocolSpec s2 = make_spec();
                rxtech::SlotWriter w(s2);
                rxtech::ProgressTracker t(s2);
                w.write(ctx, pkt);
                t.advance(ctx, 1U, ch, pi == 9U);
            }
        }

        rxtech::CpiFinalizer finalizer;
        const auto maybe = finalizer.try_finalize(ctx, rxtech::TriggerCpiSwitch);
        assert(maybe.has_value());
        const auto result = verifier.verify(*maybe, spec);

        assert(!result.passed);
        assert(rxtech::has_error(result.error_flags, rxtech::CpiVerifyError::kChannelCoverage));
        assert(result.bad_channel_prt_count >= 1U);
    }

    // ── Test 5: missing PRTs ─────────────────────────────────────────────────
    {
        // Build context expecting 3 PRTs, but only fill 2.
        auto ctx_ptr = std::make_unique<rxtech::CpiContext>();
        rxtech::CpiContext &ctx = *ctx_ptr;
        ctx.reset(5U, 0U);
        ctx.header.channels_per_prt = 3U;
        ctx.header.packets_per_channel = 9U;
        ctx.header.expected_n_prt = 3U;
        ctx.header.expected_slot_count = 3U * 3U * 9U;
        ctx.header.state = rxtech::CpiState::ACTIVE;
        ctx.header.observed_n_prt = 2U; // only 2 observed

        fill_prt(ctx, spec, 1U);
        fill_prt(ctx, spec, 2U);

        rxtech::CpiFinalizer finalizer;
        const auto maybe = finalizer.try_finalize(ctx, rxtech::TriggerCpiSwitch);
        assert(maybe.has_value());
        const auto result = verifier.verify(*maybe, spec);

        assert(!result.passed);
        assert(rxtech::has_error(result.error_flags, rxtech::CpiVerifyError::kMissingPrt));
        assert(result.missing_prt_count >= 1U);
    }

    // ── Test 6: abnormal finalize decision ──────────────────────────────────
    {
        // Build context with no data → ready_prt_count == 0 → ABNORMAL_CUTOFF_COMMIT
        auto ctx_ptr = std::make_unique<rxtech::CpiContext>();
        rxtech::CpiContext &ctx = *ctx_ptr;
        ctx.reset(6U, 0U);
        ctx.header.channels_per_prt = 3U;
        ctx.header.packets_per_channel = 9U;
        ctx.header.expected_n_prt = 2U;
        ctx.header.expected_slot_count = 2U * 3U * 9U;
        ctx.header.state = rxtech::CpiState::ACTIVE;
        ctx.header.ready_prt_count = 0U;

        rxtech::CpiFinalizer finalizer;
        const auto maybe = finalizer.try_finalize(ctx, rxtech::TriggerCpiSwitch);
        assert(maybe.has_value());
        assert(maybe->decision == rxtech::CpiDecision::ABNORMAL_CUTOFF_COMMIT);

        const auto result = verifier.verify(*maybe, spec);
        assert(!result.passed);
        assert(rxtech::has_error(result.error_flags, rxtech::CpiVerifyError::kAbnormalFinalize));
    }

    // ── Test 7: premature finalize (INCOMPLETE_BUT_COMMITTABLE) ─────────────
    {
        // Fill 1 complete PRT so ready_prt_count > 0 → INCOMPLETE_BUT_COMMITTABLE
        auto ctx_ptr = std::make_unique<rxtech::CpiContext>();
        rxtech::CpiContext &ctx = *ctx_ptr;
        ctx.reset(7U, 0U);
        ctx.header.channels_per_prt = 3U;
        ctx.header.packets_per_channel = 9U;
        ctx.header.expected_n_prt = 3U;
        ctx.header.expected_slot_count = 3U * 3U * 9U;
        ctx.header.state = rxtech::CpiState::ACTIVE;
        ctx.header.observed_n_prt = 1U;

        fill_prt(ctx, spec, 1U);

        rxtech::CpiFinalizer finalizer;
        const auto maybe = finalizer.try_finalize(ctx, rxtech::TriggerCpiSwitch);
        assert(maybe.has_value());
        assert(maybe->decision == rxtech::CpiDecision::INCOMPLETE_BUT_COMMITTABLE);

        const auto result = verifier.verify(*maybe, spec);
        assert(!result.passed);
        assert(rxtech::has_error(result.error_flags, rxtech::CpiVerifyError::kPrematureFinalize));
    }

    return 0;
}
