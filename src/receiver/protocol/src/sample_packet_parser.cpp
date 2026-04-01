#include "rxtech/sample_packet_parser.h"

namespace rxtech {

namespace {

constexpr std::uint32_t kControlTableMagic = 0x55AAFF00U;
constexpr std::uint32_t kDataPacketMagic = 0x55AAFF03U;
constexpr std::uint16_t kEtherTypeIpv4 = 0x0800U;
constexpr std::uint8_t kIpProtoUdp = 17U;

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

std::uint16_t read_u16_be(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t resolve_payload_offset(const PacketDesc& packet) {
    if (packet.len >= 14U) {
        const std::uint16_t ether_type = read_u16_be(packet.data + 12U);
        if (ether_type == kEtherTypeIpv4 && packet.len >= 42U) {
            const std::uint8_t version_ihl = packet.data[14U];
            const std::uint8_t version = static_cast<std::uint8_t>((version_ihl >> 4U) & 0x0FU);
            const std::uint8_t ihl_words = static_cast<std::uint8_t>(version_ihl & 0x0FU);
            const std::uint32_t ip_header_bytes = static_cast<std::uint32_t>(ihl_words) * 4U;
            if (version == 4U && ihl_words >= 5U && packet.len >= 14U + ip_header_bytes + 8U && packet.data[23U] == kIpProtoUdp) {
                return 14U + ip_header_bytes + 8U;
            }
        }
    }
    return 0U;
}

std::uint16_t ipv4_fragment_field(const PacketDesc& packet) {
    if (packet.len < 22U) {
        return 0U;
    }
    return read_u16_be(packet.data + 20U);
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
    if (packet.data == nullptr) {
        parsed.error_reason = "packet too short";
        return parsed;
    }

    const std::uint32_t payload_offset = resolve_payload_offset(packet);
    parsed.header_offset = payload_offset;
    parsed.frame_length = packet.len;
    const std::uint16_t fragment_field = ipv4_fragment_field(packet);
    parsed.ip_fragment_offset = static_cast<std::uint16_t>((fragment_field & 0x1FFFU) * 8U);
    parsed.more_ip_fragments = (fragment_field & 0x2000U) != 0U;
    if (packet.len < payload_offset + 16U) {
        parsed.error_reason = "packet too short";
        return parsed;
    }

    const std::uint8_t* payload = packet.data + payload_offset;
    parsed.magic = read_u32_le(payload + 0U);
    parsed.cpi = read_u16_le(payload + 4U);

    if (parsed.magic == kControlTableMagic) {
        parsed.kind = SamplePacketKind::control_table;
        parsed.payload_ptr = payload + 16U;
        parsed.payload_len = packet.len - payload_offset - 16U;
        parsed.valid = true;
        return parsed;
    }

    if (parsed.magic == kDataPacketMagic) {
        parsed.kind = SamplePacketKind::data_packet;
        parsed.channel = read_u16_le(payload + 6U);
        parsed.prt = read_u16_le(payload + 8U);
        parsed.packet_index = read_u16_le(payload + 10U);
        parsed.tail = read_u32_le(payload + 12U);
        parsed.payload_ptr = payload + 16U;
        parsed.payload_len = packet.len - payload_offset - 16U;
        parsed.valid = true;
        return parsed;
    }

    parsed.error_reason = "unknown packet magic";
    return parsed;
}

}  // namespace rxtech
