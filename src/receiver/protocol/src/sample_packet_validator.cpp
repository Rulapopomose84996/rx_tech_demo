#include "rxtech/sample_packet_validator.h"

namespace rxtech {

namespace {

constexpr std::uint32_t kExpectedPayloadBytes = 2032U;
constexpr std::uint16_t kMaxChannel = 3U;
constexpr std::uint16_t kMaxPacketIndex = 8U;
constexpr std::uint32_t kValidTail = 0x55AAFF30U;

}  // namespace

SamplePacketValidation SamplePacketValidator::validate(const SamplePacketView& packet) const noexcept {
    if (!packet.valid) {
        return {false, packet.error_reason.empty() ? "parse failed" : packet.error_reason};
    }

    if (packet.kind == SamplePacketKind::control_table) {
        if (packet.ip_fragment_offset != 0U) {
            return {false, "control table continuation fragment"};
        }
        if (packet.payload_len == 0U) {
            return {false, "empty control table payload"};
        }
        return {true, ""};
    }

    if (packet.kind == SamplePacketKind::data_packet) {
        if (packet.ip_fragment_offset != 0U) {
            return {false, "data continuation fragment"};
        }
        if (packet.channel > kMaxChannel) {
            return {false, "channel out of range"};
        }
        if (packet.packet_index > kMaxPacketIndex) {
            return {false, "packet index out of range"};
        }
        if (packet.payload_len == 0U) {
            return {false, "empty data payload"};
        }
        if (packet.tail != 0U && packet.tail != kValidTail) {
            return {false, "invalid packet tail"};
        }
        return {true, ""};
    }

    return {false, "unsupported packet kind"};
}

}  // namespace rxtech
