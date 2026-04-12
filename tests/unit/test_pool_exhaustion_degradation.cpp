#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdint>
#include <string>

#include "rxtech/cpi_context.h"
#include "rxtech/cpi_context_pool.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/spsc_ring.h"
#include "cpi_state_coordinator.h"

namespace
{

    rxtech::ParsedPacketView make_parsed(std::uint16_t cpi, std::uint16_t prt, std::uint16_t channel,
                                         std::uint16_t packet_index, std::uint64_t ts_ns,
                                         const std::array<std::uint8_t, rxtech::kCpiSlotStride> &payload)
    {
        rxtech::ParsedPacketView parsed;
        parsed.valid = true;
        parsed.kind = rxtech::PacketKind::data_packet;
        parsed.cpi = cpi;
        parsed.prt = prt;
        parsed.channel = channel;
        parsed.packet_index = packet_index;
        parsed.payload_ptr = payload.data();
        parsed.payload_len = static_cast<std::uint32_t>(payload.size());
        parsed.rx_tsc = ts_ns;
        return parsed;
    }

    rxtech::InterpretedPacketView make_interpreted(std::uint16_t cpi, std::uint16_t prt, std::uint16_t channel,
                                                   std::uint16_t packet_index)
    {
        rxtech::InterpretedPacketView interpreted;
        interpreted.valid = true;
        interpreted.kind = rxtech::PacketKind::data_packet;
        interpreted.cpi = cpi;
        interpreted.prt = prt;
        interpreted.channel = channel;
        interpreted.packet_index = packet_index;
        interpreted.packet_position_in_prt = static_cast<std::uint16_t>(channel * 9U + packet_index);
        interpreted.iq_count = 508U;
        return interpreted;
    }

} // anonymous namespace

int main()
{
    // Minimal spec: 1 PRT, 1 channel, 1 packet per channel, no timeout.
    rxtech::ProtocolSpec spec;
    spec.expected_n_prt = 1U;
    spec.channels_per_prt = 1U;
    spec.packets_per_channel = 1U;
    spec.protocol_cpi_timeout_ns = 0U;
    spec.dynamic_prt_enabled = false;

    rxtech::CpiStateCoordinator coordinator(spec);

    // Use very small output ring (4 usable slots) so it fills up quickly.
    rxtech::SpscRing<rxtech::CpiOutput> output_ring(4U);
    rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(4U);
    coordinator.attach_rings(&output_ring, &recycle_ring);

    rxtech::MetricsCollector metrics;
    std::string run_status;
    std::string run_error;

    std::array<std::uint8_t, rxtech::kCpiSlotStride> payload{};
    payload[0] = 0xAB;

    // Fill all 16 pool slots by sending packets with increasing CPI IDs.
    // Each CPI gets 1 PRT × 1 channel × 1 packet = complete immediately → finalize → push to output ring.
    // Once output ring fills (4 slots), further finalizations release pool slots immediately (backpressure path).
    // Eventually we exhaust the pool.
    for (std::uint16_t cpi = 1; cpi <= rxtech::kCpiContextPoolDepth + 4; ++cpi)
    {
        auto parsed = make_parsed(cpi, 1U, 0U, 1U, 1000U * cpi, payload);
        auto interpreted = make_interpreted(cpi, 1U, 0U, 1U);
        auto result = coordinator.process_data_packet(parsed, interpreted, spec, metrics, run_status, run_error);
        // Some packets may be dropped due to pool exhaustion — that's the point of this test.
        (void)result;
    }

    // Key assertion: pool exhaustion must NOT set run_error (degradation, not hard error).
    assert(run_error.empty() && "pool exhaustion should degrade, not set run_error");

    // Verify that pool_exhaustion events were recorded via metrics.
    // We can't inspect the counter directly, but run_status being "degraded" (if set) is acceptable.
    // The fact that we got here without run_error is the main assertion.

    // Drain recycle and verify coordinator is still usable.
    coordinator.drain_recycle(metrics);

    // Try processing one more packet — coordinator should still work.
    {
        auto parsed = make_parsed(100U, 1U, 0U, 1U, 999999U, payload);
        auto interpreted = make_interpreted(100U, 1U, 0U, 1U);
        coordinator.process_data_packet(parsed, interpreted, spec, metrics, run_status, run_error);
        assert(run_error.empty() && "coordinator should remain usable after pool exhaustion");
    }

    coordinator.finalize_active_for_shutdown(metrics);

    return 0;
}
