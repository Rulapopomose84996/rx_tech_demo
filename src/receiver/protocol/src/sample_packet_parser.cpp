#include "rxtech/sample_packet_parser.h"

namespace rxtech {

namespace {

constexpr std::uint32_t kControlTableMagic = 0x55AAFF00U;
constexpr std::uint32_t kDataPacketMagic = 0x55AAFF03U;

std::uint16_t read_u16_le(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) |
                                      (static_cast<std::uint16_t>(data[1]) << 8U));
}

std::uint32_t read_u32_le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

}  // namespace

const char* sample_packet_kind_name(SamplePacketKind kind) noexcept {
    switch (kind) {
        case SamplePacketKind::control_table:
            return "control_table";
        case SamplePacketKind::data_packet:
            return "data_packet";
        case SamplePacketKind::unknown:
            return "unknown";
    }
    return "unknown";
}

SamplePacketView SamplePacketParser::parse(const PacketDesc& packet) const noexcept {
    SamplePacketView parsed;
    if (packet.data == nullptr || packet.len < 16U) {
        parsed.error_reason = "packet too short";
        return parsed;
    }

    parsed.magic = read_u32_le(packet.data + 0U);
    parsed.cpi = read_u16_le(packet.data + 4U);

    if (parsed.magic == kControlTableMagic) {
        parsed.kind = SamplePacketKind::control_table;
        parsed.payload_ptr = packet.data + 16U;
        parsed.payload_len = packet.len - 16U;
        parsed.valid = true;
        return parsed;
    }

    if (parsed.magic == kDataPacketMagic) {
        parsed.kind = SamplePacketKind::data_packet;
        parsed.channel = read_u16_le(packet.data + 6U);
        parsed.prt = read_u16_le(packet.data + 8U);
        parsed.packet_index = read_u16_le(packet.data + 10U);
        parsed.tail = read_u32_le(packet.data + 12U);
        parsed.payload_ptr = packet.data + 16U;
        parsed.payload_len = packet.len - 16U;
        parsed.valid = true;
        return parsed;
    }

    parsed.error_reason = "unknown packet magic";
    return parsed;
}

}  // namespace rxtech
