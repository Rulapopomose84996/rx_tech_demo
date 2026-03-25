#include <cassert>
#include <cstdint>
#include <vector>

#include "rxtech/demo_protocol.h"
#include "rxtech/parser.h"

namespace {

std::vector<std::uint8_t> make_real_sender_packet(std::uint16_t flags = 0U,
                                                  std::uint64_t block_id = 0x000000000e433003ULL,
                                                  std::uint16_t frag_idx = 1U,
                                                  std::uint16_t frag_count = 3U,
                                                  std::uint16_t frag_payload_bytes = 1440U) {
    std::vector<std::uint8_t> bytes = {
        0x54, 0x50, 0x44, 0x58, 0x01, 0x00,
        static_cast<std::uint8_t>(flags & 0xFFU), static_cast<std::uint8_t>((flags >> 8U) & 0xFFU),
        0x00, 0x00, 0x00, 0x00,
        static_cast<std::uint8_t>(block_id & 0xFFU),
        static_cast<std::uint8_t>((block_id >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((block_id >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((block_id >> 24U) & 0xFFU),
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x10, 0x00, 0x00,
        static_cast<std::uint8_t>(frag_idx & 0xFFU), static_cast<std::uint8_t>((frag_idx >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(frag_count & 0xFFU), static_cast<std::uint8_t>((frag_count >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(frag_payload_bytes & 0xFFU), static_cast<std::uint8_t>((frag_payload_bytes >> 8U) & 0xFFU),
        0x00, 0x00
    };
    bytes.resize(rxtech::kDemoHeaderWireBytes + frag_payload_bytes, 0xABU);
    return bytes;
}

}  // namespace

int main() {
    {
        std::vector<std::uint8_t> bytes = make_real_sender_packet(0U, 0x000000000e433003ULL, 1U, 3U, 1440U);
        rxtech::PacketDesc packet;
        packet.data = bytes.data();
        packet.len = static_cast<std::uint32_t>(bytes.size());
        packet.port_id = 0U;
        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(meta.valid);
        assert(meta.magic == rxtech::kDemoMagic);
        assert(meta.version == rxtech::kDemoVersion);
        assert(meta.flags == 0U);
        assert(meta.stream_id == 0U);
        assert(meta.block_id == 0x000000000e433003ULL);
        assert(meta.block_bytes == 4096U);
        assert(meta.frag_idx == 1U);
        assert(meta.frag_count == 3U);
        assert(meta.frag_payload_bytes == 1440U);
    }

    {
        std::vector<std::uint8_t> bytes = make_real_sender_packet(rxtech::kDemoFlagLastFragment, 0x000000000e433003ULL, 2U, 3U, 1216U);
        rxtech::PacketDesc packet;
        packet.data = bytes.data();
        packet.len = static_cast<std::uint32_t>(bytes.size());
        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(meta.valid);
        assert(meta.flags == rxtech::kDemoFlagLastFragment);
        assert(meta.frag_idx == 2U);
        assert(meta.frag_count == 3U);
        assert(meta.frag_payload_bytes == 1216U);
    }

    {
        std::vector<std::uint8_t> payload = make_real_sender_packet(rxtech::kDemoFlagLastFragment, 0x000000000e433003ULL, 2U, 3U, 1216U);
        std::vector<std::uint8_t> frame = {
            0x9c, 0x47, 0x82, 0xe1, 0x36, 0xd0, 0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc, 0x08, 0x00,
            0x45, 0x00, 0x04, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x40, 0x11, 0x07, 0x5a, 0xac, 0x14, 0x0b, 0x0b,
            0xac, 0x14, 0x0b, 0x64, 0x0f, 0xaa, 0x27, 0x0f, 0x04, 0xe8, 0x00, 0x00
        };
        frame.insert(frame.end(), payload.begin(), payload.end());
        rxtech::PacketDesc packet;
        packet.data = frame.data();
        packet.len = static_cast<std::uint32_t>(frame.size());
        packet.port_id = 0U;
        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(meta.valid);
        assert(meta.flags == rxtech::kDemoFlagLastFragment);
        assert(meta.frag_idx == 2U);
        assert(meta.frag_count == 3U);
        assert(meta.frag_payload_bytes == 1216U);
    }

    {
        std::vector<std::uint8_t> bytes = make_real_sender_packet();
        bytes[0] = 0x00;
        rxtech::PacketDesc packet;
        packet.data = bytes.data();
        packet.len = static_cast<std::uint32_t>(bytes.size());
        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(!meta.valid);
        assert(meta.error_reason == "invalid magic");
    }

    {
        std::vector<std::uint8_t> bytes = make_real_sender_packet();
        bytes[4] = 0x02;
        rxtech::PacketDesc packet;
        packet.data = bytes.data();
        packet.len = static_cast<std::uint32_t>(bytes.size());
        const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
        assert(!meta.valid);
        assert(meta.error_reason == "unsupported version");
    }

    return 0;
}
