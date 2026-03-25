#include "rxtech/parser.h"

namespace rxtech {

namespace {

std::uint16_t read_u16_be(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t read_u32_be(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t read_u64_be(const std::uint8_t* data) {
    return (static_cast<std::uint64_t>(data[0]) << 56U) |
           (static_cast<std::uint64_t>(data[1]) << 48U) |
           (static_cast<std::uint64_t>(data[2]) << 40U) |
           (static_cast<std::uint64_t>(data[3]) << 32U) |
           (static_cast<std::uint64_t>(data[4]) << 24U) |
           (static_cast<std::uint64_t>(data[5]) << 16U) |
           (static_cast<std::uint64_t>(data[6]) << 8U) |
           static_cast<std::uint64_t>(data[7]);
}

}  // namespace

ParsedPacketMeta parse_packet(const PacketDesc& packet) {
    ParsedPacketMeta meta;
    meta.port_id = packet.port_id;
    if (packet.data == nullptr || packet.len < kDemoHeaderWireBytes) {
        meta.error_reason = "packet too short";
        return meta;
    }

    const std::uint8_t* data = packet.data;
    meta.magic = read_u32_be(data + 0U);
    meta.version = read_u16_be(data + 4U);
    meta.flags = read_u16_be(data + 6U);
    meta.stream_id = read_u32_be(data + 8U);
    meta.block_id = read_u64_be(data + 12U);
    meta.block_bytes = read_u32_be(data + 20U);
    meta.frag_idx = read_u16_be(data + 24U);
    meta.frag_count = read_u16_be(data + 26U);
    meta.frag_payload_bytes = read_u16_be(data + 28U);
    meta.payload_offset = static_cast<std::uint32_t>(kDemoHeaderWireBytes);

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

    meta.valid = true;
    return meta;
}

}  // namespace rxtech
