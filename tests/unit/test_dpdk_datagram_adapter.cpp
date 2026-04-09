#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <vector>

#include "dpdk_backend.h"

namespace
{

std::vector<std::uint8_t> make_udp_frame()
{
    return {
        0x9c, 0x47, 0x82, 0xe1, 0x36, 0xd0, 0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc, 0x08, 0x00,
        0x45, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x40, 0x11, 0x00, 0x00,
        0xac, 0x14, 0x0b, 0xde, 0xac, 0x14, 0x0b, 0x64,
        0xe4, 0x79, 0x27, 0x0f, 0x00, 0x0c, 0x00, 0x00,
        0xde, 0xad, 0xbe, 0xef};
}

std::vector<std::uint8_t> make_arp_request()
{
    return {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc,
        0x08, 0x06,
        0x00, 0x01, 0x08, 0x00, 0x06, 0x04,
        0x00, 0x01,
        0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc,
        0xac, 0x14, 0x0b, 0xde,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xac, 0x14, 0x0b, 0x64};
}

std::vector<std::uint8_t> make_tcp_frame()
{
    return {
        0x9c, 0x47, 0x82, 0xe1, 0x36, 0xd0, 0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc, 0x08, 0x00,
        0x45, 0x00, 0x00, 0x28, 0x00, 0x02, 0x00, 0x00, 0x40, 0x06, 0x00, 0x00,
        0xac, 0x14, 0x0b, 0xde, 0xac, 0x14, 0x0b, 0x64,
        0x00, 0x50, 0x27, 0x0f, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x50, 0x10, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
}

rxtech::PacketDesc make_packet_desc(const std::vector<std::uint8_t>& frame)
{
    rxtech::PacketDesc packet;
    packet.data = const_cast<std::uint8_t*>(frame.data());
    packet.len = static_cast<std::uint32_t>(frame.size());
    packet.ts_ns = 123456U;
    packet.queue_id = 7U;
    packet.cookie = 0x1234U;
    return packet;
}

}  // namespace

int main()
{
    {
        rxtech::DpdkDatagramAdapter adapter(0xac140b64U);
        rxtech::BackendStats stats;
        rxtech::UdpDatagramBurst result;
        const std::vector<std::uint8_t> frame = make_udp_frame();

        const bool adapted = adapter.adapt_frame(make_packet_desc(frame), stats, result);

        assert(adapted);
        assert(result.datagrams.size() == 1U);
        assert(result.datagrams.front().dst_port == 9999U);
    }

    {
        rxtech::DpdkDatagramAdapter adapter(0xac140b64U);
        rxtech::BackendStats stats;
        rxtech::UdpDatagramBurst result;
        const std::vector<std::uint8_t> frame = make_arp_request();

        const bool adapted = adapter.adapt_frame(make_packet_desc(frame), stats, result);

        assert(!adapted);
        assert(result.datagrams.empty());
        assert(stats.arp_request_packets == 1U);
    }

    {
        rxtech::DpdkDatagramAdapter adapter(0xac140b64U);
        rxtech::BackendStats stats;
        rxtech::UdpDatagramBurst result;
        const std::vector<std::uint8_t> frame = make_tcp_frame();

        const bool adapted = adapter.adapt_frame(make_packet_desc(frame), stats, result);

        assert(!adapted);
        assert(result.datagrams.empty());
    }

    return 0;
}
