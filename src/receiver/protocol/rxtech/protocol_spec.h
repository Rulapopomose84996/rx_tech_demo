#pragma once

#include <cstdint>

namespace rxtech {

struct RxConfig;

struct ProtocolSpec {
    std::uint32_t magic_control = 0x55AAFF00U;
    std::uint32_t magic_data = 0x55AAFF03U;
    std::uint32_t magic_tail = 0x55AAFF30U;
    std::uint32_t udp_packet_size = 2048U;
    std::uint32_t packet_header_size = 16U;
    std::uint32_t packet_data_size = 2032U;
    std::uint32_t channels_per_prt = 3U;
    std::uint32_t packets_per_channel = 9U;
    std::uint32_t iq_per_full_packet = 508U;
    std::uint32_t iq_per_last_packet = 476U;
    std::uint32_t control_table_size = 2048U;
};

ProtocolSpec protocol_spec_from_config(const RxConfig& config);

}  // namespace rxtech