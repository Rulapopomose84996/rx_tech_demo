#include "rxtech/sample_packet_validator.h"

namespace rxtech {

namespace {

constexpr std::uint32_t kExpectedPayloadBytes = 2032U;
constexpr std::uint32_t kExpectedUdpPayloadBytes = 2048U;
constexpr std::uint16_t kMaxChannel = 3U;
constexpr std::uint16_t kMinPacketIndex = 1U;
constexpr std::uint16_t kMaxPacketIndex = 9U;
constexpr std::uint32_t kValidTail = 0x55AAFF30U;

}  // namespace

SamplePacketValidation SamplePacketValidator::validate(const SamplePacketView& packet) const noexcept {
    if (!packet.valid) {
        return {false, packet.error_reason.empty() ? "parse failed" : packet.error_reason};
    }

    if (packet.kind == SamplePacketKind::control_table) {
        if (packet.ip_fragment_offset != 0U || packet.more_ip_fragments) {
            return {false, "control table is fragmented"};
        }
        if (packet.frame_length != kExpectedUdpPayloadBytes) {
            return {false, "unexpected control table payload length"};
        }
        if (packet.payload_len != kExpectedPayloadBytes || packet.payload_ptr == nullptr) {
            return {false, "unexpected control table body length"};
        }
        return {true, ""};
    }

    if (packet.kind == SamplePacketKind::data_packet) {
        if (packet.ip_fragment_offset != 0U || packet.more_ip_fragments) {
            return {false, "data packet is fragmented"};
        }
        if (packet.channel > kMaxChannel) {
            return {false, "channel out of range"};
        }
        if (packet.packet_index < kMinPacketIndex || packet.packet_index > kMaxPacketIndex) {
            return {false, "packet index out of range"};
        }
        if (packet.payload_len != kExpectedPayloadBytes) {
            return {false, "unexpected data payload length"};
        }
        if (packet.tail != 0U && packet.tail != kValidTail) {
            return {false, "invalid packet tail"};
        }
        return {true, ""};
    }

    return {false, "unsupported packet kind"};
}

}  // namespace rxtech
