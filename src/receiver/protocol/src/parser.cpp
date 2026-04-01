#include "rxtech/parser.h"

#include "rxtech/packet_parser.h"

namespace rxtech {

namespace {

std::uint16_t read_u16_be(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

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

std::uint32_t read_u32_be(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t read_u64_le(const std::uint8_t* data) {
    return static_cast<std::uint64_t>(data[0]) |
           (static_cast<std::uint64_t>(data[1]) << 8U) |
           (static_cast<std::uint64_t>(data[2]) << 16U) |
           (static_cast<std::uint64_t>(data[3]) << 24U) |
           (static_cast<std::uint64_t>(data[4]) << 32U) |
           (static_cast<std::uint64_t>(data[5]) << 40U) |
           (static_cast<std::uint64_t>(data[6]) << 48U) |
           (static_cast<std::uint64_t>(data[7]) << 56U);
}

bool resolve_payload_offset(const PacketDesc& packet, std::uint32_t& offset) {
    offset = 0U;
    if (packet.len >= 14U) {
        const std::uint16_t ether_type = read_u16_be(packet.data + 12U);
        if (ether_type == 0x0800U && packet.len >= 42U) {
            const std::uint8_t version_ihl = packet.data[14U];
            const std::uint8_t version = static_cast<std::uint8_t>((version_ihl >> 4U) & 0x0FU);
            const std::uint8_t ihl_words = static_cast<std::uint8_t>(version_ihl & 0x0FU);
            const std::uint32_t ip_header_bytes = static_cast<std::uint32_t>(ihl_words) * 4U;
            if (version == 4U && ihl_words >= 5U && packet.len >= 14U + ip_header_bytes + 8U && packet.data[23U] == 17U) {
                offset = 14U + ip_header_bytes + 8U;
            }
        }
    }
    return true;
}

}  // namespace

ParsedPacketMeta parse_packet(const PacketDesc& packet) {
    ParsedPacketMeta meta;
    meta.port_id = packet.port_id;
    if (packet.data == nullptr) {
        meta.error_reason = "packet too short";
        return meta;
    }

    std::uint32_t payload_offset = 0U;
    resolve_payload_offset(packet, payload_offset);
    if (packet.len < payload_offset + kDemoHeaderWireBytes) {
        meta.error_reason = "packet too short";
        return meta;
    }

    const std::uint8_t* data = packet.data + payload_offset;
    meta.magic = read_u32_be(data + 0U);
    meta.version = read_u16_le(data + 4U);
    meta.flags = read_u16_le(data + 6U);
    meta.stream_id = read_u32_le(data + 8U);
    meta.block_id = read_u64_le(data + 12U);
    meta.block_bytes = read_u32_le(data + 20U);
    meta.frag_idx = read_u16_le(data + 24U);
    meta.frag_count = read_u16_le(data + 26U);
    meta.frag_payload_bytes = read_u16_le(data + 28U);
    meta.payload_offset = payload_offset + static_cast<std::uint32_t>(kDemoHeaderWireBytes);

    if (meta.magic != kDemoMagic) {
        meta.error_reason = "invalid magic";
        return meta;
    }
    if (meta.version != kDemoVersion) {
        meta.error_reason = "unsupported version";
        return meta;
    }
    if (meta.frag_count == 0U) {
        meta.error_reason = "invalid fragment count";
        return meta;
    }
    if (meta.frag_idx >= meta.frag_count) {
        meta.error_reason = "fragment index out of range";
        return meta;
    }

    const std::uint32_t payload_bytes = packet.len - meta.payload_offset;
    if (meta.frag_payload_bytes > payload_bytes) {
        meta.error_reason = "fragment payload exceeds packet";
        return meta;
    }
    if (meta.block_bytes == 0U) {
        meta.error_reason = "invalid block bytes";
        return meta;
    }

    meta.valid = true;
    return meta;
}

ParsedPacketMeta PacketParser::parse(const PacketDesc& packet) const {
    return parse_packet(packet);
}

}  // namespace rxtech
