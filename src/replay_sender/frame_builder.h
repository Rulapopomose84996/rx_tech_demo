#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rxtech::replay
{

    /// Network configuration for wrapping a raw UDP payload in Ethernet/IPv4/UDP headers.
    struct FrameConfig
    {
        std::array<std::uint8_t, 6> src_mac{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
        std::array<std::uint8_t, 6> dst_mac{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        std::uint32_t src_ipv4_be = 0xAC140BDEU; ///< 172.20.11.222 big-endian
        std::uint32_t dst_ipv4_be = 0xAC140B64U; ///< 172.20.11.100 big-endian
        std::uint16_t src_port = 30001U;
        std::uint16_t dst_port = 9999U;
    };

    /// Build a complete Ethernet/IPv4/UDP frame from a raw UDP payload.
    ///
    /// The resulting byte vector can be handed directly to AF_PACKET or
    /// stored as a PacketDesc buffer for the FileReplayBackend.
    ///
    /// @param payload  The 2048-byte UDP payload (control table or data packet).
    /// @param cfg      Network addressing parameters.
    /// @param seq      Packet sequence number (used as IPv4 identification field).
    /// @returns        Fully formed Ethernet frame including headers and payload.
    std::vector<std::uint8_t> build_eth_frame(const std::uint8_t *payload,
                                              std::uint32_t payload_len,
                                              const FrameConfig &cfg,
                                              std::uint16_t seq = 0);

} // namespace rxtech::replay
