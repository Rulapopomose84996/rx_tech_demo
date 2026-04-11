#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <string>

#include "rxtech/cpi_context.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/spsc_ring.h"
#include "rxtech/cpi_finalizer.h"
#include "cpi_state_coordinator.h"

namespace
{

    // Dummy payload buffer for slot writer (must be non-null and have valid length)
    static std::uint8_t g_dummy_payload[2048] = {};

    rxtech::ProtocolSpec make_spec(bool dynamic_prt, std::uint32_t expected_n_prt, std::uint32_t max_n_prt)
    {
        rxtech::ProtocolSpec spec;
        spec.channels_per_prt = 3U;
        spec.packets_per_channel = 9U;
        spec.dynamic_prt_enabled = dynamic_prt;
        spec.expected_n_prt = expected_n_prt;
        spec.max_n_prt = max_n_prt;
        spec.protocol_cpi_timeout_ns = 0U;
        return spec;
    }

    rxtech::ParsedPacketView make_control(std::uint16_t cpi, std::uint16_t n_prt_in_control)
    {
        rxtech::ParsedPacketView pv;
        pv.valid = true;
        pv.kind = rxtech::PacketKind::control_table;
        pv.cpi = cpi;
        // n_prt from control packet is carried in the prt field
        pv.prt = n_prt_in_control;
        return pv;
    }

    rxtech::ParsedPacketView make_data(std::uint16_t cpi, std::uint16_t prt, std::uint16_t channel,
                                       std::uint16_t packet_index)
    {
        rxtech::ParsedPacketView pv;
        pv.valid = true;
        pv.kind = rxtech::PacketKind::data_packet;
        pv.cpi = cpi;
        pv.prt = prt;
        pv.channel = channel;
        pv.packet_index = packet_index;
        pv.payload_ptr = g_dummy_payload;
        pv.payload_len = 2032U;
        return pv;
    }

    rxtech::InterpretedPacketView make_interpreted_data(std::uint16_t cpi, std::uint16_t prt, std::uint16_t channel,
                                                        std::uint16_t packet_index)
    {
        rxtech::InterpretedPacketView iv;
        iv.valid = true;
        iv.kind = rxtech::PacketKind::data_packet;
        iv.cpi = cpi;
        iv.prt = prt;
        iv.channel = channel;
        iv.packet_index = packet_index;
        iv.iq_count = 508U;
        return iv;
    }

    rxtech::CpiOutput finalize_via_switch(rxtech::CpiStateCoordinator &coord,
                                          rxtech::SpscRing<rxtech::CpiOutput> &output_ring,
                                          const rxtech::ProtocolSpec &spec, rxtech::MetricsCollector &metrics,
                                          std::string &status, std::string &error, std::uint16_t next_cpi,
                                          std::uint16_t tail_cpi)
    {
        const auto next_result = coord.process_data_packet(
            make_data(next_cpi, 1U, 0U, 1U), make_interpreted_data(next_cpi, 1U, 0U, 1U), spec, metrics, status, error);
        assert(next_result.accepted);

        const auto tail_result = coord.process_data_packet(
            make_data(tail_cpi, 1U, 0U, 1U), make_interpreted_data(tail_cpi, 1U, 0U, 1U), spec, metrics, status, error);
        assert(tail_result.accepted);

        rxtech::CpiOutput output;
        assert(output_ring.pop(output));
        return output;
    }

} // namespace

int main()
{
    // Test 1: Control packet first → BOUND (n_prt from control)
    {
        auto spec = make_spec(true, 10U, 100U);
        rxtech::CpiStateCoordinator coord(spec);
        constexpr std::size_t kRingCap = 8U;
        rxtech::SpscRing<rxtech::CpiOutput> output_ring(kRingCap);
        rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(kRingCap);
        coord.attach_rings(&output_ring, &recycle_ring);

        rxtech::MetricsCollector metrics;
        std::string status = "success";
        std::string error;

        // Send control packet with n_prt=20
        coord.process_control_packet(make_control(1U, 20U));

        // Send data packet → open_active triggers
        auto parsed = make_data(1U, 1U, 0U, 1U);
        auto interpreted = make_interpreted_data(1U, 1U, 0U, 1U);
        auto result = coord.process_data_packet(parsed, interpreted, spec, metrics, status, error);
        assert(result.accepted);

        const auto output = finalize_via_switch(coord, output_ring, spec, metrics, status, error, 2U, 3U);
        assert(output.cpi_id == 1U);
        assert(output.control.cpi_id == 1U);
        assert(output.control.n_prt == 20U);
        assert(output.control.bind_source == rxtech::BindSource::control);
        assert(!output.control.conflict);

        coord.release_active();
    }

    // Test 2: Data first → PROVISIONAL → Control converges → BOUND
    {
        auto spec = make_spec(true, 10U, 64U);
        rxtech::CpiStateCoordinator coord(spec);
        constexpr std::size_t kRingCap = 8U;
        rxtech::SpscRing<rxtech::CpiOutput> output_ring(kRingCap);
        rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(kRingCap);
        coord.attach_rings(&output_ring, &recycle_ring);

        rxtech::MetricsCollector metrics;
        std::string status = "success";
        std::string error;

        // Data first → provisional
        auto parsed = make_data(1U, 5U, 0U, 1U);
        auto interpreted = make_interpreted_data(1U, 5U, 0U, 1U);
        auto result = coord.process_data_packet(parsed, interpreted, spec, metrics, status, error);
        assert(result.accepted);

        // Control packet with n_prt=30 (>= observed max prt=5) → converge
        coord.process_control_packet(make_control(1U, 30U));

        const auto output = finalize_via_switch(coord, output_ring, spec, metrics, status, error, 2U, 3U);
        assert(output.control.cpi_id == 1U);
        assert(output.control.n_prt == 30U);
        assert(output.control.bind_source == rxtech::BindSource::control);
        assert(!output.control.conflict);

        coord.release_active();
    }

    // Test 3: Control n_prt < observed max prt → CONFLICT
    {
        auto spec = make_spec(true, 10U, 64U);
        rxtech::CpiStateCoordinator coord(spec);
        constexpr std::size_t kRingCap = 8U;
        rxtech::SpscRing<rxtech::CpiOutput> output_ring(kRingCap);
        rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(kRingCap);
        coord.attach_rings(&output_ring, &recycle_ring);

        rxtech::MetricsCollector metrics;
        std::string status = "success";
        std::string error;

        // Data with prt=10 → provisional
        auto parsed = make_data(1U, 10U, 0U, 1U);
        auto interpreted = make_interpreted_data(1U, 10U, 0U, 1U);
        const auto result = coord.process_data_packet(parsed, interpreted, spec, metrics, status, error);
        assert(result.accepted);

        // Control says n_prt=5, but we've seen prt=10 → conflict
        coord.process_control_packet(make_control(1U, 5U));

        const auto output = finalize_via_switch(coord, output_ring, spec, metrics, status, error, 2U, 3U);
        assert(output.control.cpi_id == 1U);
        assert(output.control.bind_source == rxtech::BindSource::provisional);
        assert(output.control.conflict);
        assert(output.control.n_prt == 64U);

        coord.release_active();
    }

    // Test 4: dynamic_prt_enabled=false → uses expected_n_prt (backward compatible)
    {
        auto spec = make_spec(false, 25U, 100U);
        rxtech::CpiStateCoordinator coord(spec);
        constexpr std::size_t kRingCap = 8U;
        rxtech::SpscRing<rxtech::CpiOutput> output_ring(kRingCap);
        rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(kRingCap);
        coord.attach_rings(&output_ring, &recycle_ring);

        rxtech::MetricsCollector metrics;
        std::string status = "success";
        std::string error;

        // Control packet with n_prt=50 — should be ignored when dynamic=false
        coord.process_control_packet(make_control(1U, 50U));

        auto parsed = make_data(1U, 1U, 0U, 1U);
        auto interpreted = make_interpreted_data(1U, 1U, 0U, 1U);
        auto result = coord.process_data_packet(parsed, interpreted, spec, metrics, status, error);
        assert(result.accepted);

        const auto output = finalize_via_switch(coord, output_ring, spec, metrics, status, error, 2U, 3U);
        assert(output.control.cpi_id == 1U);
        assert(output.control.bind_source == rxtech::BindSource::fixed);
        assert(output.control.n_prt == 25U);

        coord.release_active();
    }

    // Test 5: invalid control n_prt → fallback to expected_n_prt
    {
        auto spec = make_spec(true, 15U, 50U);
        rxtech::CpiStateCoordinator coord(spec);
        constexpr std::size_t kRingCap = 8U;
        rxtech::SpscRing<rxtech::CpiOutput> output_ring(kRingCap);
        rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(kRingCap);
        coord.attach_rings(&output_ring, &recycle_ring);

        rxtech::MetricsCollector metrics;
        std::string status = "success";
        std::string error;

        // Control packet with n_prt=0 (invalid)
        coord.process_control_packet(make_control(1U, 0U));

        const auto first = coord.process_data_packet(make_data(1U, 1U, 0U, 1U), make_interpreted_data(1U, 1U, 0U, 1U),
                                                     spec, metrics, status, error);
        assert(first.accepted);

        const auto output = finalize_via_switch(coord, output_ring, spec, metrics, status, error, 2U, 3U);
        assert(output.control.cpi_id == 1U);
        assert(output.control.bind_source == rxtech::BindSource::fixed);
        assert(output.control.n_prt == 15U);
        assert(output.control.valid);

        coord.release_active();
    }

    // Test 6: CPI dual-window — late packet accepted
    {
        auto spec = make_spec(false, 25U, 100U);
        rxtech::CpiStateCoordinator coord(spec);
        constexpr std::size_t kRingCap = 8U;
        rxtech::SpscRing<rxtech::CpiOutput> output_ring(kRingCap);
        rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(kRingCap);
        coord.attach_rings(&output_ring, &recycle_ring);

        rxtech::MetricsCollector metrics;
        std::string status = "success";
        std::string error;

        // Open CPI 1 with some data
        auto parsed1 = make_data(1U, 1U, 0U, 1U);
        auto interp1 = make_interpreted_data(1U, 1U, 0U, 1U);
        coord.process_data_packet(parsed1, interp1, spec, metrics, status, error);

        // Switch to CPI 2 — CPI 1 becomes previous
        auto parsed2 = make_data(2U, 1U, 0U, 1U);
        auto interp2 = make_interpreted_data(2U, 1U, 0U, 1U);
        coord.process_data_packet(parsed2, interp2, spec, metrics, status, error);

        // Late packet for CPI 1 — should be accepted via previous window
        auto late = make_data(1U, 1U, 1U, 1U);
        auto late_interp = make_interpreted_data(1U, 1U, 1U, 1U);
        auto late_result = coord.process_data_packet(late, late_interp, spec, metrics, status, error);
        assert(late_result.accepted);

        const auto summary = metrics.finalize("test", "test", "test", 1U);
        assert(summary.performance.late_packet_accepted_count >= 1U);

        coord.release_active();
    }

    // Test 7: finalized CPI must not be reopened before recycle drains
    {
        auto spec = make_spec(false, 1U, 1U);
        spec.channels_per_prt = 1U;
        spec.packets_per_channel = 1U;
        rxtech::CpiStateCoordinator coord(spec);
        constexpr std::size_t kRingCap = 8U;
        rxtech::SpscRing<rxtech::CpiOutput> output_ring(kRingCap);
        rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(kRingCap);
        coord.attach_rings(&output_ring, &recycle_ring);

        rxtech::MetricsCollector metrics;
        std::string status = "success";
        std::string error;

        const auto first = coord.process_data_packet(make_data(1U, 1U, 0U, 1U), make_interpreted_data(1U, 1U, 0U, 1U),
                                                     spec, metrics, status, error);
        assert(first.accepted);

        rxtech::CpiOutput first_output;
        assert(output_ring.pop(first_output));
        assert(first_output.cpi_id == 1U);

        // Before recycle is drained, another packet from the same finalized CPI
        // must be treated as late-to-closed/drop rather than opening a new active context.
        for (int i = 0; i < 8; ++i)
        {
            const auto late = coord.process_data_packet(
                make_data(1U, 1U, 0U, 1U), make_interpreted_data(1U, 1U, 0U, 1U), spec, metrics, status, error);
            assert(!late.accepted);
            assert(status == "success");
            assert(error.empty());
        }

        coord.release_active();
    }

    return 0;
}
