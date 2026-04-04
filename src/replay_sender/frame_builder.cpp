#include "frame_builder.h"

#include <cstdint>
#include <vector>

namespace rxtech::replay
{

    namespace
    {
        std::uint16_t ipv4_checksum(const std::uint8_t *hdr, std::size_t len)
        {
            std::uint32_t sum = 0;
            for (std::size_t i = 0; i < len; i += 2)
            {
                const std::uint16_t word =
                    (static_cast<std::uint16_t>(hdr[i]) << 8U) |
                    static_cast<std::uint16_t>(hdr[i + 1]);
                sum += word;
            }
            while (sum >> 16U)
                sum = (sum & 0xFFFFU) + (sum >> 16U);
            return static_cast<std::uint16_t>(~sum);
        }
    } // namespace

    std::vector<std::uint8_t> build_eth_frame(const std::uint8_t *payload,
                                              std::uint32_t payload_len,
                                              const FrameConfig &cfg,
                                              std::uint16_t seq)
    {
        // Size constants
        constexpr std::size_t kEthHdrSize = 14U;
        constexpr std::size_t kIpHdrSize = 20U;
        constexpr std::size_t kUdpHdrSize = 8U;

        const std::uint16_t udp_length =
            static_cast<std::uint16_t>(kUdpHdrSize + payload_len);
        const std::uint16_t ip_total =
            static_cast<std::uint16_t>(kIpHdrSize + udp_length);

        std::vector<std::uint8_t> frame;
        frame.reserve(kEthHdrSize + kIpHdrSize + kUdpHdrSize + payload_len);

        // ── Ethernet header (14 bytes) ────────────────────────────────────────
        frame.insert(frame.end(), cfg.dst_mac.begin(), cfg.dst_mac.end());
        frame.insert(frame.end(), cfg.src_mac.begin(), cfg.src_mac.end());
        frame.push_back(0x08); // EtherType IPv4
        frame.push_back(0x00);

        // ── IPv4 header (20 bytes, options-free) ─────────────────────────────
        const std::size_t ip_offset = frame.size();
        frame.push_back(0x45); // version=4, IHL=5
        frame.push_back(0x00); // DSCP/ECN
        frame.push_back(static_cast<std::uint8_t>((ip_total >> 8U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(ip_total & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>((seq >> 8U) & 0xFFU)); // identification hi
        frame.push_back(static_cast<std::uint8_t>(seq & 0xFFU));         // identification lo
        frame.push_back(0x40);                                           // flags: Don't Fragment
        frame.push_back(0x00);                                           // fragment offset
        frame.push_back(0x40);                                           // TTL 64
        frame.push_back(0x11);                                           // protocol: UDP
        frame.push_back(0x00);                                           // checksum hi (placeholder)
        frame.push_back(0x00);                                           // checksum lo (placeholder)
        frame.push_back(static_cast<std::uint8_t>((cfg.src_ipv4_be >> 24U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>((cfg.src_ipv4_be >> 16U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>((cfg.src_ipv4_be >> 8U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(cfg.src_ipv4_be & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>((cfg.dst_ipv4_be >> 24U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>((cfg.dst_ipv4_be >> 16U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>((cfg.dst_ipv4_be >> 8U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(cfg.dst_ipv4_be & 0xFFU));

        // Fill IPv4 checksum
        const std::uint16_t ipcsum = ipv4_checksum(frame.data() + ip_offset, kIpHdrSize);
        frame[ip_offset + 10] = static_cast<std::uint8_t>((ipcsum >> 8U) & 0xFFU);
        frame[ip_offset + 11] = static_cast<std::uint8_t>(ipcsum & 0xFFU);

        // ── UDP header (8 bytes) ─────────────────────────────────────────────
        frame.push_back(static_cast<std::uint8_t>((cfg.src_port >> 8U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(cfg.src_port & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>((cfg.dst_port >> 8U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(cfg.dst_port & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>((udp_length >> 8U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(udp_length & 0xFFU));
        frame.push_back(0x00); // UDP checksum not required for loopback
        frame.push_back(0x00);

        // ── Payload ───────────────────────────────────────────────────────────
        frame.insert(frame.end(), payload, payload + payload_len);

        return frame;
    }

} // namespace rxtech::replay
