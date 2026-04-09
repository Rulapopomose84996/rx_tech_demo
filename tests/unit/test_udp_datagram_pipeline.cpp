#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cstdint>

#include "rxtech/packet_desc.h"

int main()
{
    rxtech::UdpDatagramBurst burst;
    std::array<std::uint8_t, 4> payload{0x03U, 0xFFU, 0xAAU, 0x55U};

    rxtech::UdpDatagramDesc desc;
    desc.payload_data = payload.data();
    desc.payload_len = static_cast<std::uint32_t>(payload.size());
    desc.src_ipv4_be = 0x7F000001U;
    desc.dst_ipv4_be = 0x7F000001U;
    desc.src_port = 40000U;
    desc.dst_port = 9999U;
    desc.backend_kind = rxtech::BackendKind::socket;

    burst.datagrams.push_back(desc);

    if (burst.datagrams.size() != 1U || burst.datagrams.front().payload_len != 4U)
    {
        return 1;
    }
    burst.datagrams.clear();
    return burst.datagrams.empty() ? 0 : 1;
}
