#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <vector>

#include "dpdk_backend.h"

namespace
{

constexpr std::uint64_t kTimestampNs = 123456U;
constexpr std::uint32_t kQueueId = 7U;
constexpr std::uintptr_t kCookie = 0x1234U;
constexpr std::uint32_t kSourceIpv4Be = 0xac140bdeU;
constexpr std::uint32_t kDestIpv4Be = 0xac140b64U;
constexpr std::uint16_t kSourcePort = 0xe479U;
constexpr std::uint16_t kDestPort = 0x270fU;

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
    packet.ts_ns = kTimestampNs;
    packet.queue_id = kQueueId;
    packet.cookie = kCookie;
    return packet;
}

void assert_rejected(const std::vector<std::uint8_t>& frame)
{
    rxtech::DpdkDatagramAdapter adapter(kDestIpv4Be);
    rxtech::BackendStats stats;
    rxtech::UdpDatagramDesc datagram;

    const bool adapted = adapter.adapt_frame(make_packet_desc(frame), stats, datagram);

    assert(!adapted);
    assert(datagram.payload_data == nullptr);
    assert(datagram.payload_len == 0U);
    assert(datagram.raw_frame_data == nullptr);
    assert(datagram.raw_frame_len == 0U);
}

}  // namespace

int main()
{
    {
        rxtech::DpdkDatagramAdapter adapter(kDestIpv4Be);
        rxtech::BackendStats stats;
        rxtech::UdpDatagramDesc datagram;
        const std::vector<std::uint8_t> frame = make_udp_frame();

        const bool adapted = adapter.adapt_frame(make_packet_desc(frame), stats, datagram);

        assert(adapted);
        assert(datagram.payload_data == frame.data() + 42U);
        assert(datagram.payload_len == 4U);
        assert(datagram.raw_frame_data == frame.data());
        assert(datagram.raw_frame_len == frame.size());
        assert(datagram.src_ipv4_be == kSourceIpv4Be);
        assert(datagram.dst_ipv4_be == kDestIpv4Be);
        assert(datagram.src_port == kSourcePort);
        assert(datagram.dst_port == kDestPort);
        assert(datagram.ts_ns == kTimestampNs);
        assert(datagram.queue_id == kQueueId);
        assert(datagram.cookie == kCookie);
        assert(datagram.backend_kind == rxtech::BackendKind::dpdk);
    }

    {
        rxtech::DpdkDatagramAdapter adapter(kDestIpv4Be);
        rxtech::BackendStats stats;
        rxtech::UdpDatagramDesc datagram;
        const std::vector<std::uint8_t> frame = make_arp_request();

        const bool adapted = adapter.adapt_frame(make_packet_desc(frame), stats, datagram);

        assert(!adapted);
        assert(datagram.payload_data == nullptr);
        assert(datagram.raw_frame_data == nullptr);
        assert(stats.arp_request_packets == 1U);
    }

    assert_rejected(make_tcp_frame());

    {
        std::vector<std::uint8_t> short_ethernet = {0x00, 0x01, 0x02, 0x03, 0x04};
        assert_rejected(short_ethernet);
    }

    {
        std::vector<std::uint8_t> invalid_ihl = make_udp_frame();
        invalid_ihl[14] = 0x44;
        assert_rejected(invalid_ihl);
    }

    {
        std::vector<std::uint8_t> truncated_total_length = make_udp_frame();
        truncated_total_length[16] = 0x00;
        truncated_total_length[17] = 0x30;
        assert_rejected(truncated_total_length);
    }

    {
        std::vector<std::uint8_t> udp_length_mismatch = make_udp_frame();
        udp_length_mismatch[38] = 0x00;
        udp_length_mismatch[39] = 0x20;
        assert_rejected(udp_length_mismatch);
    }

    {
        std::vector<std::uint8_t> fragmented = make_udp_frame();
        fragmented[20] = 0x20;
        fragmented[21] = 0x00;
        assert_rejected(fragmented);
    }

    return 0;
}
