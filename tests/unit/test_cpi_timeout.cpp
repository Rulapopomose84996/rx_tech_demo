#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdint>
#include <string>

#include "rxtech/cpi_context.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/sample_packet_parser.h"
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

    void recycle_output(rxtech::CpiStateCoordinator &coordinator, rxtech::SpscRing<rxtech::ReleaseToken> &recycle_ring,
                        rxtech::MetricsCollector &metrics, const rxtech::CpiOutput &output)
    {
        rxtech::ReleaseToken token;
        token.output_id = output.output_id;
        token.ctx_pool_index = output.pool_index;
        assert(recycle_ring.push(token));
        coordinator.drain_recycle(metrics);
    }

} // namespace

int main()
{
    rxtech::ProtocolSpec spec;
    spec.expected_n_prt = 1U;
    spec.dynamic_prt_enabled = false;
    spec.protocol_cpi_timeout_ns = 100U;

    rxtech::CpiStateCoordinator coordinator(spec);
    rxtech::SpscRing<rxtech::CpiOutput> output_ring(8U);
    rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(8U);
    coordinator.attach_rings(&output_ring, &recycle_ring);

    rxtech::MetricsCollector metrics;
    std::string run_status = "success";
    std::string run_error;
    std::array<std::uint8_t, rxtech::kCpiSlotStride> payload{};

    {
        const rxtech::ParsedPacketView parsed = make_parsed(1U, 1U, 0U, 1U, 1000U, payload);
        const rxtech::InterpretedPacketView interpreted = make_interpreted(1U, 1U, 0U, 1U);
        const rxtech::CpiProcessResult result =
            coordinator.process_data_packet(parsed, interpreted, spec, metrics, run_status, run_error);
        assert(result.accepted);
        assert(!coordinator.check_timeout(1099U, metrics));

        rxtech::CpiOutput output;
        assert(!output_ring.pop(output));

        assert(coordinator.check_timeout(1100U, metrics));
        assert(output_ring.pop(output));
        assert(output.cpi_id == 1U);
        assert(output.decision == rxtech::CpiDecision::ABNORMAL_CUTOFF_COMMIT);
        assert((output.trigger_bits & rxtech::TriggerTimeout) != 0U);
        recycle_output(coordinator, recycle_ring, metrics, output);
    }

    {
        const rxtech::ParsedPacketView parsed1 = make_parsed(10U, 1U, 0U, 1U, 2000U, payload);
        const rxtech::InterpretedPacketView interpreted1 = make_interpreted(10U, 1U, 0U, 1U);
        const rxtech::ParsedPacketView parsed2 = make_parsed(11U, 1U, 0U, 1U, 2050U, payload);
        const rxtech::InterpretedPacketView interpreted2 = make_interpreted(11U, 1U, 0U, 1U);

        assert(coordinator.process_data_packet(parsed1, interpreted1, spec, metrics, run_status, run_error).accepted);
        assert(coordinator.process_data_packet(parsed2, interpreted2, spec, metrics, run_status, run_error).accepted);

        assert(coordinator.check_timeout(2100U, metrics));
        rxtech::CpiOutput output;
        assert(output_ring.pop(output));
        assert(output.cpi_id == 10U);
        assert((output.trigger_bits & rxtech::TriggerTimeout) != 0U);
        recycle_output(coordinator, recycle_ring, metrics, output);

        coordinator.finalize_active_for_shutdown(metrics);
        assert(output_ring.pop(output));
        recycle_output(coordinator, recycle_ring, metrics, output);
    }

    return 0;
}
