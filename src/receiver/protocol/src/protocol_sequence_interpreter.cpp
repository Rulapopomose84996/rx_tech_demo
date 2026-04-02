#include "rxtech/protocol_sequence_interpreter.h"

namespace rxtech {

namespace {

constexpr std::uint16_t kPacketsPerChannel = 9U;
constexpr std::uint16_t kExpectedChannels = 3U;
constexpr std::uint16_t kExpectedPacketsPerPrt = kPacketsPerChannel * kExpectedChannels;
constexpr std::uint32_t kFullPacketIq = 508U;
constexpr std::uint32_t kNinthPacketIq = 476U;
constexpr std::uint32_t kBytesPerIq = 4U;

const char* channel_name(std::uint16_t channel) {
    switch (channel) {
        case 0:
            return "和路";
        case 1:
            return "俯仰差";
        case 2:
            return "方位差";
        default:
            return "未知通道";
    }
}

}  // namespace

ProtocolPacketView ProtocolSequenceInterpreter::interpret(const SamplePacketView& packet) noexcept {
    ProtocolPacketView result;
    result.kind = packet.kind;
    result.cpi = packet.cpi;

    if (!packet.valid) {
        result.error_reason = packet.error_reason.empty() ? "parse failed" : packet.error_reason;
        return result;
    }

    if (packet.kind == SamplePacketKind::control_table) {
        result.valid = true;
        return result;
    }

    if (packet.kind != SamplePacketKind::data_packet) {
        result.error_reason = "unsupported packet kind";
        return result;
    }

    std::uint32_t& counter = data_packet_counters_[packet.cpi];
    ++counter;
    const std::uint16_t position_in_prt =
        static_cast<std::uint16_t>(((counter - 1U) % kExpectedPacketsPerPrt) + 1U);
    result.packet_position_in_prt = position_in_prt;
    result.prt = static_cast<std::uint16_t>(((counter - 1U) / kExpectedPacketsPerPrt) + 1U);

    result.channel = static_cast<std::uint16_t>((position_in_prt - 1U) / kPacketsPerChannel);
    result.channel_name = channel_name(result.channel);
    result.packet_index = static_cast<std::uint16_t>(((position_in_prt - 1U) % kPacketsPerChannel) + 1U);

    if (packet.payload_len != 2032U || packet.payload_ptr == nullptr) {
        result.error_reason = "unexpected data payload length";
        return result;
    }

    if (result.packet_index == 9U) {
        result.iq_count = kNinthPacketIq;
        result.zero_padding_bytes = packet.payload_len - (kNinthPacketIq * kBytesPerIq);
        for (std::uint32_t index = kNinthPacketIq * kBytesPerIq; index < packet.payload_len; ++index) {
            if (packet.payload_ptr[index] != 0U) {
                result.error_reason = "ninth packet zero padding is not all zero";
                return result;
            }
        }
    } else {
        result.iq_count = kFullPacketIq;
        result.zero_padding_bytes = 0U;
    }

    result.valid = true;
    return result;
}

}  // namespace rxtech
