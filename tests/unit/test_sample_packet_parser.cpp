#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <vector>

#include "rxtech/sample_packet_parser.h"

namespace
{

    std::vector<std::uint8_t> make_udp_frame_with_sample_payload()
    {
        std::vector<std::uint8_t> bytes = {
            0x9c, 0x47, 0x82, 0xe1, 0x36, 0xd0, 0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc, 0x08, 0x00,
            0x45, 0x00, 0x08, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x40, 0x11, 0x00, 0x00,
            0xac, 0x14, 0x0b, 0xde, 0xac, 0x14, 0x0b, 0x64,
            0xe4, 0x79, 0x27, 0x0f, 0x08, 0x08, 0x00, 0x00,
            0x03, 0xff, 0xaa, 0x55,
            0x01, 0x00,
            0x00, 0x00,
            0x22, 0x00,
            0x02, 0x00,
            0x00, 0x00, 0x00, 0x00};
        bytes.resize(14U + 20U + 8U + 2048U, 0xABU);
        return bytes;
    }

} // namespace

int main()
{
    const std::vector<std::uint8_t> frame = make_udp_frame_with_sample_payload();
    rxtech::PacketDesc packet;
    packet.data = const_cast<std::uint8_t *>(frame.data());
    packet.len = static_cast<std::uint32_t>(frame.size());
    packet.ts_ns = 123456U;

    rxtech::PacketParser parser;
    const rxtech::ParsedPacketView parsed = parser.parse(packet);

    assert(parsed.valid);
    assert(parsed.kind == rxtech::PacketKind::data_packet);
    assert(parsed.cpi == 1U);
    assert(parsed.channel == 0U);
    assert(parsed.prt == 34U);
    assert(parsed.packet_index == 2U);
    assert(parsed.payload_len == 2032U);
    assert(parsed.payload_ptr == frame.data() + 14U + 20U + 8U + 16U);
    assert(parsed.rx_tsc == 123456U);
    assert(parsed.reject_reason == rxtech::RejectReason::none);
    return 0;
}
