#include <cstdint>
#include <vector>

#include "arp_responder.h"

int main() {
    std::vector<std::uint8_t> arp_request = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc,
        0x08, 0x06,
        0x00, 0x01, 0x08, 0x00, 0x06, 0x04,
        0x00, 0x01,
        0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc,
        0xac, 0x14, 0x0b, 0xde,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xac, 0x14, 0x0b, 0x64
    };

    const std::array<std::uint8_t, 6> local_mac{{0x9c, 0x47, 0x82, 0xe1, 0x36, 0xd0}};
    const std::uint32_t local_ip = 0xac140b64U; // 172.20.11.100 big-endian bytes

    rxtech::ArpRequestInfo request{};
    const bool parsed = rxtech::parse_arp_request(arp_request.data(), arp_request.size(), local_ip, request);
    if (!parsed)
    {
        return 1;
    }
    if (request.sender_ip_be != 0xac140bdeU || request.target_ip_be != 0xac140b64U)
    {
        return 1;
    }

    const std::vector<std::uint8_t> reply = rxtech::build_arp_reply(request, local_mac);
    if (reply.size() != 42U)
    {
        return 1;
    }
    if (reply[0] != 0x9cU || reply[1] != 0x47U)
    {
        return 1;
    }
    if (reply[6] != 0x9cU || reply[7] != 0x47U)
    {
        return 1;
    }
    if (reply[20] != 0x00U || reply[21] != 0x02U)
    {
        return 1;
    }
    if (reply[28] != 0xacU || reply[29] != 0x14U || reply[30] != 0x0bU || reply[31] != 0x64U)
    {
        return 1;
    }
    return 0;
}
