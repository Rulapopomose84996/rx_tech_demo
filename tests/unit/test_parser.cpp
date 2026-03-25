#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "rxtech/demo_protocol.h"
#include "rxtech/parser.h"

namespace {

void append_u16_be(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u64_be(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::vector<std::uint8_t> make_demo_packet(std::uint32_t magic = rxtech::kDemoMagic,
                                           std::uint16_t version = rxtech::kDemoVersion,
                                           std::uint16_t flags = rxtech::kDemoFlagFirstFragment |
                                                                 rxtech::kDemoFlagLastFragment) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(rxtech::kDemoHeaderWireBytes + 4U);
    append_u32_be(bytes, magic);
    append_u16_be(bytes, version);
    append_u16_be(bytes, flags);
    append_u32_be(bytes, 7U);
    append_u64_be(bytes, 42U);
    append_u32_be(bytes, 4U);
    append_u16_be(bytes, 0U);
    append_u16_be(bytes, 1U);
    append_u16_be(bytes, 4U);
    append_u16_be(bytes, 0U);
    bytes.insert(bytes.end(), {0xAAU, 0xBBU, 0xCCU, 0xDDU});
    return bytes;
}

}  // namespace

int main() {
    {
        std::vector<std::uint8_t> bytes = make_demo_packet();
        rxtech::PacketDesc packet;
        packet.data = bytes.data();
        packet.len = static_cast<std::uint32_t>(bytes.size());
        packet.port_id = 2U;

        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(meta.valid);
        assert(meta.port_id == 2U);
        assert(meta.magic == rxtech::kDemoMagic);
        assert(meta.version == rxtech::kDemoVersion);
        assert(meta.flags == (rxtech::kDemoFlagFirstFragment | rxtech::kDemoFlagLastFragment));
        assert(meta.stream_id == 7U);
        assert(meta.block_id == 42U);
        assert(meta.block_bytes == 4U);
        assert(meta.frag_idx == 0U);
        assert(meta.frag_count == 1U);
        assert(meta.frag_payload_bytes == 4U);
        assert(meta.error_reason.empty());
    }

    {
        std::vector<std::uint8_t> bytes = make_demo_packet(0xDEADBEEFU);
        rxtech::PacketDesc packet;
        packet.data = bytes.data();
        packet.len = static_cast<std::uint32_t>(bytes.size());
        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(!meta.valid);
        assert(meta.error_reason == "invalid magic");
    }

    {
        std::vector<std::uint8_t> bytes = make_demo_packet(rxtech::kDemoMagic, 9U);
        rxtech::PacketDesc packet;
        packet.data = bytes.data();
        packet.len = static_cast<std::uint32_t>(bytes.size());
        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(!meta.valid);
        assert(meta.error_reason == "unsupported version");
    }

    {
        std::vector<std::uint8_t> bytes = make_demo_packet();
        bytes.resize(rxtech::kDemoHeaderWireBytes - 1U);
        rxtech::PacketDesc packet;
        packet.data = bytes.data();
        packet.len = static_cast<std::uint32_t>(bytes.size());
        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(!meta.valid);
        assert(meta.error_reason == "packet too short");
    }

    return 0;
}
