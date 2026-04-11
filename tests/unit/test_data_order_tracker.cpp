#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "../../src/receiver/protocol/data_order_tracker.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_spec.h"

namespace
{
    rxtech::InterpretedPacketView make_packet(std::uint16_t cpi, std::uint16_t prt, std::uint16_t channel,
                                              std::uint16_t packet_index)
    {
        rxtech::InterpretedPacketView packet;
        packet.valid = true;
        packet.kind = rxtech::PacketKind::data_packet;
        packet.cpi = cpi;
        packet.prt = prt;
        packet.channel = channel;
        packet.packet_index = packet_index;
        return packet;
    }
} // namespace

int main()
{
    rxtech::ProtocolSpec spec;
    spec.channels_per_prt = 3U;
    spec.packets_per_channel = 9U;

    rxtech::DataOrderTracker tracker(spec);

    tracker.observe(make_packet(1U, 64U, 2U, 8U));
    tracker.observe(make_packet(1U, 64U, 2U, 9U));
    tracker.observe(make_packet(2U, 1U, 0U, 1U));

    rxtech::RunSummary summary;
    tracker.populate_summary(summary);

    assert(summary.data_order.checked_packets == 3U);
    assert(summary.data_order.assessment == "符合按 PRT 推进的和/差/差顺序");
    assert(summary.data_order.first_mismatch.empty());
    return 0;
}
