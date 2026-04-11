#include "arp_responder.h"

#include <algorithm>

namespace rxtech {

namespace {

constexpr std::uint16_t kEtherTypeArp = 0x0806U;
constexpr std::uint16_t kHwTypeEthernet = 0x0001U;
constexpr std::uint16_t kProtoTypeIpv4 = 0x0800U;
constexpr std::uint16_t kOpcodeRequest = 0x0001U;
constexpr std::uint16_t kOpcodeReply = 0x0002U;

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

void write_u16_be(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    data[1] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_u32_be(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    data[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    data[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    data[3] = static_cast<std::uint8_t>(value & 0xFFU);
}

}  // namespace

bool parse_arp_request(const std::uint8_t* frame,
                       std::size_t frame_len,
                       std::uint32_t local_ip_be,
                       ArpRequestInfo& out) noexcept {
    if (frame == nullptr || frame_len < 42U) {
        return false;
    }
    if (read_u16_be(frame + 12U) != kEtherTypeArp) {
        return false;
    }
    if (read_u16_be(frame + 14U) != kHwTypeEthernet) {
        return false;
    }
    if (read_u16_be(frame + 16U) != kProtoTypeIpv4) {
        return false;
    }
    if (frame[18U] != 6U || frame[19U] != 4U) {
        return false;
    }
    if (read_u16_be(frame + 20U) != kOpcodeRequest) {
        return false;
    }

    const std::uint32_t target_ip_be = read_u32_be(frame + 38U);
    if (target_ip_be != local_ip_be) {
        return false;
    }

    std::copy(frame + 22U, frame + 28U, out.sender_mac.begin());
    out.sender_ip_be = read_u32_be(frame + 28U);
    out.target_ip_be = target_ip_be;
    return true;
}

std::vector<std::uint8_t> build_arp_reply(const ArpRequestInfo& request,
                                          const std::array<std::uint8_t, 6>& local_mac) {
    std::vector<std::uint8_t> reply(42U, 0U);

    std::copy(request.sender_mac.begin(), request.sender_mac.end(), reply.begin() + 0U);
    std::copy(local_mac.begin(), local_mac.end(), reply.begin() + 6U);
    write_u16_be(reply.data() + 12U, kEtherTypeArp);
    write_u16_be(reply.data() + 14U, kHwTypeEthernet);
    write_u16_be(reply.data() + 16U, kProtoTypeIpv4);
    reply[18U] = 6U;
    reply[19U] = 4U;
    write_u16_be(reply.data() + 20U, kOpcodeReply);
    std::copy(local_mac.begin(), local_mac.end(), reply.begin() + 22U);
    write_u32_be(reply.data() + 28U, request.target_ip_be);
    std::copy(request.sender_mac.begin(), request.sender_mac.end(), reply.begin() + 32U);
    write_u32_be(reply.data() + 38U, request.sender_ip_be);
    return reply;
}

}  // namespace rxtech
