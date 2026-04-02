#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include "rxtech/packet_desc.h"

namespace rxtech
{

    struct UdpPayloadFrame
    {
        std::vector<std::uint8_t> udp_payload;
        std::uint32_t source_ipv4_be = 0;
        std::uint32_t dest_ipv4_be = 0;
        std::uint16_t source_port = 0;
        std::uint16_t dest_port = 0;
    };

    class UdpPayloadAssembler
    {
    public:
        std::vector<UdpPayloadFrame> push(const PacketDesc &packet);

    private:
        struct FragmentKey
        {
            std::uint32_t source_ipv4_be = 0;
            std::uint32_t dest_ipv4_be = 0;
            std::uint16_t identification = 0;
            std::uint8_t protocol = 0;

            bool operator==(const FragmentKey &other) const;
        };

        struct FragmentKeyHash
        {
            std::size_t operator()(const FragmentKey &key) const noexcept;
        };

        struct FragmentAssembly
        {
            std::map<std::uint16_t, std::vector<std::uint8_t>> pieces;
            std::uint16_t total_payload_length = 0;
            bool has_total_length = false;
            std::uint16_t source_port = 0;
            std::uint16_t dest_port = 0;
            std::uint16_t udp_length = 0;
            bool has_udp_header = false;
        };

        std::unordered_map<FragmentKey, FragmentAssembly, FragmentKeyHash> fragments_;
    };

} // namespace rxtech
