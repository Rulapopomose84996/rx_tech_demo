#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

#include "rxtech/cpi_context.h"
#include "rxtech/progress_tracker.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/slot_writer.h"

int main()
{
    rxtech::ProtocolSpec spec;
    spec.channels_per_prt = 3U;
    spec.packets_per_channel = 9U;

    auto context_ptr = std::make_unique<rxtech::CpiContext>();
    rxtech::CpiContext &context = *context_ptr;
    context.reset(42U, 0U);
    context.header.channels_per_prt = static_cast<std::uint16_t>(spec.channels_per_prt);
    context.header.packets_per_channel = static_cast<std::uint16_t>(spec.packets_per_channel);

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

    rxtech::SlotWriter writer(spec);
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

    rxtech::set_expected_prt_count(context, 1U,
                                   static_cast<std::uint16_t>(spec.channels_per_prt),
                                   static_cast<std::uint16_t>(spec.packets_per_channel));
    rxtech::ProgressTracker tracker(spec);
    for (std::uint16_t channel = 0; channel < spec.channels_per_prt; ++channel)
    {
        for (std::uint16_t packet_index = 1U; packet_index <= spec.packets_per_channel; ++packet_index)
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
            tracker.advance(context, 0U, channel, packet_index == spec.packets_per_channel);
        }
    }
    assert(context.header.ready_prt_count == 1U);
    assert((context.header.trigger_bits & rxtech::TriggerFullReady) != 0U);
    assert((context.header.trigger_bits & rxtech::TriggerTailObserved) != 0U);

    // V-006: PRT out of range must be rejected when expected_n_prt is set
    {
        auto ctx2_ptr = std::make_unique<rxtech::CpiContext>();
        rxtech::CpiContext &ctx2 = *ctx2_ptr;
        ctx2.reset(99U, 0U);
        ctx2.header.channels_per_prt = static_cast<std::uint16_t>(spec.channels_per_prt);
        ctx2.header.packets_per_channel = static_cast<std::uint16_t>(spec.packets_per_channel);
        rxtech::set_expected_prt_count(ctx2, 4U,
                                       static_cast<std::uint16_t>(spec.channels_per_prt),
                                       static_cast<std::uint16_t>(spec.packets_per_channel));
        assert(ctx2.header.expected_n_prt == 4U);

        rxtech::ParsedPacketView prt_ok_pkt;
        prt_ok_pkt.valid = true;
        prt_ok_pkt.kind = rxtech::PacketKind::data_packet;
        prt_ok_pkt.cpi = 99U;
        prt_ok_pkt.prt = 3U; // within [0,3]
        prt_ok_pkt.channel = 0U;
        prt_ok_pkt.packet_index = 1U;
        prt_ok_pkt.payload_ptr = payload.data();
        prt_ok_pkt.payload_len = static_cast<std::uint32_t>(payload.size());
        const rxtech::SlotWriteResult prt_ok = writer.write(ctx2, prt_ok_pkt);
        assert(prt_ok.first_write);

        rxtech::ParsedPacketView prt_bad_pkt = prt_ok_pkt;
        prt_bad_pkt.prt = 4U; // out of range
        prt_bad_pkt.packet_index = 1U;
        const rxtech::SlotWriteResult prt_bad = writer.write(ctx2, prt_bad_pkt);
        assert(!prt_bad.first_write);
        assert(prt_bad.reason == rxtech::RejectReason::invalid_prt);
    }

    // T-005: TriggerWaveEnd when tail seen on last expected PRT
    {
        auto ctx3_ptr = std::make_unique<rxtech::CpiContext>();
        rxtech::CpiContext &ctx3 = *ctx3_ptr;
        ctx3.reset(200U, 0U);
        ctx3.header.channels_per_prt = static_cast<std::uint16_t>(spec.channels_per_prt);
        ctx3.header.packets_per_channel = static_cast<std::uint16_t>(spec.packets_per_channel);
        rxtech::set_expected_prt_count(ctx3, 2U,
                                       static_cast<std::uint16_t>(spec.channels_per_prt),
                                       static_cast<std::uint16_t>(spec.packets_per_channel));
        rxtech::ProgressTracker tracker3(spec);

        // Tail on PRT 0 (not last) — should NOT set WaveEnd
        tracker3.advance(ctx3, 0U, 0U, true);
        assert((ctx3.header.trigger_bits & rxtech::TriggerTailObserved) != 0U);
        assert((ctx3.header.trigger_bits & rxtech::TriggerWaveEnd) == 0U);

        // Tail on PRT 1 (last expected) — should set WaveEnd
        tracker3.advance(ctx3, 1U, 0U, true);
        assert((ctx3.header.trigger_bits & rxtech::TriggerWaveEnd) != 0U);
    }

    return 0;
}
