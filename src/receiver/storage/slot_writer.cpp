#include "rxtech/slot_writer.h"

#include <cstring>

namespace rxtech
{

    SlotWriteResult SlotWriter::write(CpiContext &ctx, const ParsedPacketView &packet) const noexcept
    {
        SlotWriteResult result;
        if (!packet.valid || packet.kind != PacketKind::data_packet)
        {
            result.reason = RejectReason::invalid_field_combo;
            return result;
        }
        const auto ch_count = static_cast<std::uint16_t>(spec_.channels_per_prt);
        const auto pkt_per_ch = static_cast<std::uint16_t>(spec_.packets_per_channel);
        if (!is_valid_slot_coord(packet.prt, packet.channel, packet.packet_index, ch_count, pkt_per_ch))
        {
            result.reason = packet.channel >= ch_count ? RejectReason::invalid_channel
                                                       : RejectReason::invalid_packet_index;
            return result;
        }
        // V-006: PRT range check against expected N_PRT (if known)
        if (ctx.header.expected_n_prt > 0U &&
            packet.prt >= ctx.header.expected_n_prt)
        {
            result.reason = RejectReason::invalid_prt;
            return result;
        }
        if (packet.payload_ptr == nullptr || packet.payload_len == 0U || packet.payload_len > kCpiSlotStride)
        {
            result.reason = RejectReason::invalid_len;
            return result;
        }

        result.slot_index = slot_index(packet.prt, packet.channel, packet.packet_index, ch_count, pkt_per_ch);
        PrtSummary &prt_summary = ctx.prt_summary[packet.prt];
        const std::uint16_t bit = packet_bit(packet.packet_index);
        std::uint16_t &bitmap = prt_summary.ch_pkt_bitmap[packet.channel];
        if ((bitmap & bit) != 0U)
        {
            ++ctx.header.duplicate_count;
            result.duplicate = true;
            return result;
        }

        std::memcpy(ctx.slot_payload(result.slot_index), packet.payload_ptr, packet.payload_len);
        ctx.slot_valid_bytes[result.slot_index] = static_cast<std::uint16_t>(packet.payload_len);
        bitmap = static_cast<std::uint16_t>(bitmap | bit);
        ++prt_summary.ch_recv_count[packet.channel];
        ++prt_summary.recv_packet_count;
        ++ctx.header.received_slot_count;
        if (packet.rx_tsc != 0U)
        {
            if (ctx.header.first_rx_tsc == 0U)
            {
                ctx.header.first_rx_tsc = packet.rx_tsc;
            }
            ctx.header.last_rx_tsc = packet.rx_tsc;
        }
        if (packet.prt + 1U > ctx.header.observed_n_prt)
        {
            ctx.header.observed_n_prt = static_cast<std::uint16_t>(packet.prt + 1U);
        }
        result.first_write = true;
        return result;
    }

} // namespace rxtech
