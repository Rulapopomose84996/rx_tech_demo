#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <vector>

#include "rxtech/cpi_context.h"
#include "rxtech/progress_tracker.h"
#include "rxtech/slot_writer.h"

int main()
{
    rxtech::CpiContext context;
    context.reset(42U, 0U);

    std::vector<std::uint8_t> payload(2032U, 0x5AU);
    rxtech::ParsedPacketView packet;
    packet.valid = true;
    packet.kind = rxtech::PacketKind::data_packet;
    packet.cpi = 42U;
    packet.channel = 2U;
    packet.prt = 0U;
    packet.packet_index = 5U;
    packet.payload_ptr = payload.data();
    packet.payload_len = static_cast<std::uint32_t>(payload.size());
    packet.rx_tsc = 100U;

    rxtech::SlotWriter writer;
    const rxtech::SlotWriteResult write_result = writer.write(context, packet);
    assert(write_result.first_write);
    assert(!write_result.duplicate);
    assert(write_result.slot_index == 22U);
    assert(context.slot_valid_bytes[write_result.slot_index] == 2032U);
    assert(context.prt_summary[0].ch_pkt_bitmap[2] == (1U << 4U));
    assert(context.header.received_slot_count == 1U);

    const rxtech::SlotWriteResult duplicate_result = writer.write(context, packet);
    assert(!duplicate_result.first_write);
    assert(duplicate_result.duplicate);
    assert(context.header.duplicate_count == 1U);

    rxtech::set_expected_prt_count(context, 1U);
    rxtech::ProgressTracker tracker;
    for (std::uint16_t channel = 0; channel < rxtech::kCpiChannelCount; ++channel)
    {
        for (std::uint16_t packet_index = 1U; packet_index <= rxtech::kCpiPacketsPerChannel; ++packet_index)
        {
            if (channel == 2U && packet_index == 5U)
            {
                tracker.advance(context, 0U, channel, false);
                continue;
            }
            packet.channel = channel;
            packet.packet_index = packet_index;
            const rxtech::SlotWriteResult current = writer.write(context, packet);
            assert(current.first_write);
            tracker.advance(context, 0U, channel, packet_index == rxtech::kCpiPacketsPerChannel);
        }
    }
    assert(context.header.ready_prt_count == 1U);
    assert((context.header.trigger_bits & rxtech::TriggerFullReady) != 0U);
    assert((context.header.trigger_bits & rxtech::TriggerTailObserved) != 0U);
    return 0;
}
