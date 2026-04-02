#include "rxtech/protocol_sequence_interpreter.h"

namespace rxtech {

namespace {

constexpr std::uint32_t kBytesPerIq = 4U;

}  // namespace

InterpretedPacketView ProtocolSequenceInterpreter::interpret(const ParsedPacketView& packet) const noexcept {
    InterpretedPacketView result;
    result.kind = packet.kind;
    result.cpi = packet.cpi;

    if (!packet.valid) {
        result.reject_reason = packet.reject_reason == RejectReason::none ? RejectReason::invalid_header : packet.reject_reason;
        return result;
    }

    if (packet.kind == PacketKind::control_table) {
        result.valid = true;
        return result;
    }

    if (packet.kind != PacketKind::data_packet) {
        result.reject_reason = RejectReason::invalid_field_combo;
        return result;
    }

    if (packet.channel >= spec_.channels_per_prt) {
        result.reject_reason = RejectReason::invalid_channel;
        return result;
    }
    if (packet.packet_index == 0U || packet.packet_index > spec_.packets_per_channel) {
        result.reject_reason = RejectReason::invalid_packet_index;
        return result;
    }

    result.prt = packet.prt;
    result.channel = packet.channel;
    result.packet_index = packet.packet_index;
    result.packet_position_in_prt = static_cast<std::uint16_t>(packet.channel * spec_.packets_per_channel + packet.packet_index);

    if (packet.payload_len != spec_.packet_data_size || packet.payload_ptr == nullptr) {
        result.reject_reason = RejectReason::invalid_len;
        return result;
    }

    if (result.packet_index == spec_.packets_per_channel) {
        result.iq_count = spec_.iq_per_last_packet;
        const std::uint32_t used_bytes = spec_.iq_per_last_packet * kBytesPerIq;
        if (used_bytes > packet.payload_len) {
            result.reject_reason = RejectReason::invalid_len;
            return result;
        }
        result.zero_padding_bytes = packet.payload_len - used_bytes;
        for (std::uint32_t index = used_bytes; index < packet.payload_len; ++index) {
            if (packet.payload_ptr[index] != 0U) {
                result.reject_reason = RejectReason::invalid_field_combo;
                return result;
            }
        }
    } else {
        result.iq_count = spec_.iq_per_full_packet;
        result.zero_padding_bytes = 0U;
    }

    result.valid = true;
    return result;
}

}  // namespace rxtech
