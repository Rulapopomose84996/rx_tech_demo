#include <cstdint>
#include <iostream>
#include <vector>

#include "rxtech/packet_desc.h"
#include "rxtech/udp_payload_assembler.h"

namespace {

std::vector<std::uint8_t> make_ipv4_udp_frame(const std::vector<std::uint8_t>& udp_payload,
                                              std::uint16_t identification,
                                              bool more_fragments,
                                              std::uint16_t fragment_offset_bytes,
                                              std::uint16_t full_udp_length = 0U) {
    const std::uint16_t udp_length = full_udp_length != 0U
        ? full_udp_length
        : static_cast<std::uint16_t>(8U + udp_payload.size());
    const std::uint16_t ip_payload_length =
        fragment_offset_bytes == 0U ? static_cast<std::uint16_t>(8U + udp_payload.size())
                                    : static_cast<std::uint16_t>(udp_payload.size());
    const std::uint16_t total_length = static_cast<std::uint16_t>(20U + ip_payload_length);
    const std::uint16_t fragment_field =
        static_cast<std::uint16_t>(((fragment_offset_bytes / 8U) & 0x1FFFU) | (more_fragments ? 0x2000U : 0U));

    std::vector<std::uint8_t> bytes = {
        0x9c, 0x47, 0x82, 0xe1, 0x36, 0xd0, 0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc, 0x08, 0x00,
        0x45, 0x00,
        static_cast<std::uint8_t>((total_length >> 8U) & 0xFFU), static_cast<std::uint8_t>(total_length & 0xFFU),
        static_cast<std::uint8_t>((identification >> 8U) & 0xFFU), static_cast<std::uint8_t>(identification & 0xFFU),
        static_cast<std::uint8_t>((fragment_field >> 8U) & 0xFFU), static_cast<std::uint8_t>(fragment_field & 0xFFU),
        0x40, 0x11, 0x00, 0x00,
        0xac, 0x14, 0x0b, 0xde,
        0xac, 0x14, 0x0b, 0x64,
    };

    if (fragment_offset_bytes == 0U) {
        bytes.push_back(0x75U);
        bytes.push_back(0x30U);
        bytes.push_back(0x27U);
        bytes.push_back(0x0FU);
        bytes.push_back(static_cast<std::uint8_t>((udp_length >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(udp_length & 0xFFU));
        bytes.push_back(0x00U);
        bytes.push_back(0x00U);
    }

    bytes.insert(bytes.end(), udp_payload.begin(), udp_payload.end());
    return bytes;
}

}  // namespace

int main() {
    rxtech::UdpPayloadAssembler assembler;

    {
        std::vector<std::uint8_t> udp_payload(2048U, 0xABU);
        udp_payload[0] = 0x03U;
        udp_payload[1] = 0xFFU;
        udp_payload[2] = 0xAAU;
        udp_payload[3] = 0x55U;
        const std::vector<std::uint8_t> frame = make_ipv4_udp_frame(udp_payload, 0x1001U, false, 0U);

        rxtech::PacketDesc packet;
        packet.data = const_cast<std::uint8_t*>(frame.data());
        packet.len = static_cast<std::uint32_t>(frame.size());

        const auto results = assembler.push(packet);
        if (results.size() != 1U || results[0].udp_payload.size() != 2048U) {
            std::cerr << "expected unfragmented UDP payload to pass through intact\n";
            return 1;
        }
    }

    {
        std::vector<std::uint8_t> udp_payload(2048U, 0xCDU);
        udp_payload[0] = 0x03U;
        udp_payload[1] = 0xFFU;
        udp_payload[2] = 0xAAU;
        udp_payload[3] = 0x55U;

        const std::vector<std::uint8_t> first_payload(udp_payload.begin(), udp_payload.begin() + 1480U);
        const std::vector<std::uint8_t> second_payload(udp_payload.begin() + 1480U, udp_payload.end());

        const std::uint16_t full_udp_length = static_cast<std::uint16_t>(8U + udp_payload.size());
        const std::vector<std::uint8_t> first_fragment = make_ipv4_udp_frame(first_payload, 0x1002U, true, 0U, full_udp_length);
        const std::vector<std::uint8_t> second_fragment = make_ipv4_udp_frame(second_payload, 0x1002U, false, 1488U, full_udp_length);

        rxtech::PacketDesc first_packet;
        first_packet.data = const_cast<std::uint8_t*>(first_fragment.data());
        first_packet.len = static_cast<std::uint32_t>(first_fragment.size());

        rxtech::PacketDesc second_packet;
        second_packet.data = const_cast<std::uint8_t*>(second_fragment.data());
        second_packet.len = static_cast<std::uint32_t>(second_fragment.size());

        const auto first_results = assembler.push(first_packet);
        if (!first_results.empty()) {
            std::cerr << "expected first fragment alone to produce no complete UDP payload\n";
            return 1;
        }

        const auto second_results = assembler.push(second_packet);
        if (second_results.size() != 1U || second_results[0].udp_payload.size() != 2048U) {
            std::cerr << "expected two fragments to reassemble into one 2048-byte UDP payload\n";
            return 1;
        }
    }

    return 0;
}
