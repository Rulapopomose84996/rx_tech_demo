#include "rxtech/udp_payload_assembler.h"

#include <cstddef>

namespace rxtech
{

    namespace
    {

        constexpr std::uint16_t kEtherTypeIpv4 = 0x0800U;
        constexpr std::uint8_t kIpProtoUdp = 17U;

        std::uint16_t read_u16_be(const std::uint8_t *data)
        {
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                              static_cast<std::uint16_t>(data[1]));
        }

        std::uint32_t read_u32_be(const std::uint8_t *data)
        {
            return (static_cast<std::uint32_t>(data[0]) << 24U) |
                   (static_cast<std::uint32_t>(data[1]) << 16U) |
                   (static_cast<std::uint32_t>(data[2]) << 8U) |
                   static_cast<std::uint32_t>(data[3]);
        }

    } // namespace

    bool UdpPayloadAssembler::FragmentKey::operator==(const FragmentKey &other) const
    {
        return source_ipv4_be == other.source_ipv4_be &&
               dest_ipv4_be == other.dest_ipv4_be &&
               identification == other.identification &&
               protocol == other.protocol;
    }

    std::size_t UdpPayloadAssembler::FragmentKeyHash::operator()(const FragmentKey &key) const noexcept
    {
        return static_cast<std::size_t>(key.source_ipv4_be) ^
               (static_cast<std::size_t>(key.dest_ipv4_be) << 1U) ^
               (static_cast<std::size_t>(key.identification) << 2U) ^
               (static_cast<std::size_t>(key.protocol) << 3U);
    }

    std::vector<UdpPayloadFrame> UdpPayloadAssembler::push(const PacketDesc &packet)
    {
        std::vector<UdpPayloadFrame> results;
        if (packet.data == nullptr || packet.len < 14U + 20U)
        {
            return results;
        }

        if (read_u16_be(packet.data + 12U) != kEtherTypeIpv4)
        {
            return results;
        }

        const std::size_t ip_offset = 14U;
        const std::uint8_t version_ihl = packet.data[ip_offset];
        const std::uint8_t version = static_cast<std::uint8_t>(version_ihl >> 4U);
        const std::uint8_t ihl_words = static_cast<std::uint8_t>(version_ihl & 0x0FU);
        const std::size_t ip_header_bytes = static_cast<std::size_t>(ihl_words) * 4U;
        if (version != 4U || ihl_words < 5U || packet.len < ip_offset + ip_header_bytes)
        {
            return results;
        }

        const std::uint16_t total_length = read_u16_be(packet.data + ip_offset + 2U);
        if (packet.len < ip_offset + total_length || total_length < ip_header_bytes)
        {
            return results;
        }

        const std::uint8_t protocol = packet.data[ip_offset + 9U];
        if (protocol != kIpProtoUdp)
        {
            return results;
        }

        const std::uint32_t source_ipv4_be = read_u32_be(packet.data + ip_offset + 12U);
        const std::uint32_t dest_ipv4_be = read_u32_be(packet.data + ip_offset + 16U);
        const std::uint16_t identification = read_u16_be(packet.data + ip_offset + 4U);
        const std::uint16_t flags_and_offset = read_u16_be(packet.data + ip_offset + 6U);
        const bool more_fragments = (flags_and_offset & 0x2000U) != 0U;
        const std::uint16_t fragment_offset_bytes = static_cast<std::uint16_t>((flags_and_offset & 0x1FFFU) * 8U);

        const std::uint8_t *ip_payload = packet.data + ip_offset + ip_header_bytes;
        const std::size_t ip_payload_length = static_cast<std::size_t>(total_length) - ip_header_bytes;

        if (fragment_offset_bytes == 0U && !more_fragments)
        {
            if (ip_payload_length < 8U)
            {
                return results;
            }
            const std::uint16_t udp_length = read_u16_be(ip_payload + 4U);
            if (udp_length < 8U || ip_payload_length < udp_length)
            {
                return results;
            }

            UdpPayloadFrame frame;
            frame.source_ipv4_be = source_ipv4_be;
            frame.dest_ipv4_be = dest_ipv4_be;
            frame.source_port = read_u16_be(ip_payload + 0U);
            frame.dest_port = read_u16_be(ip_payload + 2U);
            frame.udp_payload.assign(ip_payload + 8U, ip_payload + udp_length);
            results.push_back(std::move(frame));
            return results;
        }

        FragmentKey key{source_ipv4_be, dest_ipv4_be, identification, protocol};
        FragmentAssembly &assembly = fragments_[key];
        assembly.pieces[fragment_offset_bytes] =
            std::vector<std::uint8_t>(ip_payload, ip_payload + ip_payload_length);

        if (fragment_offset_bytes == 0U && ip_payload_length >= 8U)
        {
            assembly.source_port = read_u16_be(ip_payload + 0U);
            assembly.dest_port = read_u16_be(ip_payload + 2U);
            assembly.udp_length = read_u16_be(ip_payload + 4U);
            assembly.has_udp_header = true;
        }

        if (!more_fragments)
        {
            assembly.total_payload_length = static_cast<std::uint16_t>(fragment_offset_bytes + ip_payload_length);
            assembly.has_total_length = true;
        }

        if (!assembly.has_total_length || !assembly.has_udp_header)
        {
            return results;
        }

        std::vector<std::uint8_t> assembled_ip_payload;
        assembled_ip_payload.reserve(assembly.total_payload_length);
        std::uint16_t offset = 0U;
        while (offset < assembly.total_payload_length)
        {
            const auto it = assembly.pieces.find(offset);
            if (it == assembly.pieces.end())
            {
                return results;
            }
            assembled_ip_payload.insert(assembled_ip_payload.end(), it->second.begin(), it->second.end());
            offset = static_cast<std::uint16_t>(offset + it->second.size());
        }

        if (assembly.udp_length < 8U || assembled_ip_payload.size() < assembly.udp_length)
        {
            fragments_.erase(key);
            return results;
        }

        UdpPayloadFrame frame;
        frame.source_ipv4_be = source_ipv4_be;
        frame.dest_ipv4_be = dest_ipv4_be;
        frame.source_port = assembly.source_port;
        frame.dest_port = assembly.dest_port;
        frame.udp_payload.assign(assembled_ip_payload.begin() + 8U,
                                 assembled_ip_payload.begin() + assembly.udp_length);
        results.push_back(std::move(frame));
        fragments_.erase(key);
        return results;
    }

} // namespace rxtech
