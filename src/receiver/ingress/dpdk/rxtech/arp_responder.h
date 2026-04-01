#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rxtech {

struct ArpRequestInfo {
    std::array<std::uint8_t, 6> sender_mac{};
    std::uint32_t sender_ip_be = 0;
    std::uint32_t target_ip_be = 0;
};

bool parse_arp_request(const std::uint8_t* frame,
                       std::size_t frame_len,
                       std::uint32_t local_ip_be,
                       ArpRequestInfo& out) noexcept;

std::vector<std::uint8_t> build_arp_reply(const ArpRequestInfo& request,
                                          const std::array<std::uint8_t, 6>& local_mac);

}  // namespace rxtech
