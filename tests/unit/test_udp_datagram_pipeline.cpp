#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <vector>

#include "../../src/receiver/protocol/packet_pipeline.h"
#include "../../src/receiver/protocol/udp_datagram_pipeline.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/rx_config.h"

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
    const std::uint8_t *payload_ptr = frame.data() + 14U + 20U + 8U;
    constexpr std::uint32_t payload_len = 2048U;
    constexpr std::uint32_t source_ipv4_be = 0xac140bdeU;
    constexpr std::uint32_t dest_ipv4_be = 0xac140b64U;
    constexpr std::uint16_t source_port = 0xe479U;
    constexpr std::uint16_t dest_port = 0x270fU;
    constexpr std::uint64_t ts_ns = 123456U;
    constexpr std::uint32_t queue_id = 7U;
    constexpr std::uintptr_t cookie = 0x1234U;

    const rxtech::RxConfig config = rxtech::load_default_config();
    const rxtech::ProtocolSpec spec = rxtech::protocol_spec_from_config(config);
    rxtech::PacketPipeline legacy_pipeline(config, spec);
    rxtech::UdpDatagramPipeline pipeline(config, spec);
    rxtech::MetricsCollector legacy_metrics;
    rxtech::MetricsCollector datagram_metrics;
    std::uint32_t legacy_invalid_dumped = 0;
    std::uint32_t invalid_dumped = 0;
    std::size_t legacy_callback_count = 0U;
    std::size_t callback_count = 0U;
    rxtech::PacketKind legacy_kind = rxtech::PacketKind::unknown;
    rxtech::PacketKind observed_kind = rxtech::PacketKind::unknown;
    std::vector<std::uint8_t> legacy_payload;
    std::vector<std::uint8_t> observed_payload;
    std::uint32_t legacy_source_ipv4_be = 0U;
    std::uint32_t observed_source_ipv4_be = 0U;
    std::uint32_t legacy_dest_ipv4_be = 0U;
    std::uint32_t observed_dest_ipv4_be = 0U;
    std::uint16_t legacy_source_port = 0U;
    std::uint16_t observed_source_port = 0U;
    std::uint16_t legacy_dest_port = 0U;
    std::uint16_t observed_dest_port = 0U;
    std::uint32_t legacy_queue_id = 0U;
    std::uint32_t observed_queue_id = 0U;
    std::uint64_t legacy_ts_ns = 0U;
    std::uint64_t observed_ts_ns = 0U;

    rxtech::PacketDesc packet;
    packet.data = const_cast<std::uint8_t *>(frame.data());
    packet.len = static_cast<std::uint32_t>(frame.size());
    packet.ts_ns = ts_ns;
    packet.queue_id = queue_id;
    packet.cookie = cookie;

    const auto legacy_stats = legacy_pipeline.process_packet(
        packet,
        legacy_metrics,
        nullptr,
        legacy_invalid_dumped,
        [&](const rxtech::ProcessedPacket &processed)
        {
            ++legacy_callback_count;
            legacy_kind = processed.interpreted.kind;
            legacy_payload.assign(processed.udp_frame.udp_payload.begin(), processed.udp_frame.udp_payload.end());
            legacy_source_ipv4_be = processed.udp_frame.source_ipv4_be;
            legacy_dest_ipv4_be = processed.udp_frame.dest_ipv4_be;
            legacy_source_port = processed.udp_frame.source_port;
            legacy_dest_port = processed.udp_frame.dest_port;
            legacy_queue_id = processed.source_queue_id;
            legacy_ts_ns = processed.source_ts_ns;
        });

    assert(legacy_stats.accepted_packets == 1U);
    assert(legacy_callback_count == 1U);
    assert(legacy_kind == rxtech::PacketKind::data_packet);
    assert(legacy_payload.size() == 2048U);
    assert(legacy_source_ipv4_be == source_ipv4_be);
    assert(legacy_dest_ipv4_be == dest_ipv4_be);
    assert(legacy_source_port == source_port);
    assert(legacy_dest_port == dest_port);
    assert(legacy_queue_id == queue_id);
    assert(legacy_ts_ns == ts_ns);

    rxtech::UdpDatagramDesc datagram;
    datagram.payload_data = payload_ptr;
    datagram.payload_len = payload_len;
    datagram.src_ipv4_be = source_ipv4_be;
    datagram.dst_ipv4_be = dest_ipv4_be;
    datagram.src_port = source_port;
    datagram.dst_port = dest_port;
    datagram.ts_ns = ts_ns;
    datagram.queue_id = queue_id;
    datagram.cookie = cookie;
    datagram.backend_kind = rxtech::BackendKind::socket;

    const auto stats = pipeline.process_datagram(
        datagram,
        datagram_metrics,
        nullptr,
        invalid_dumped,
        [&](const rxtech::ProcessedPacket &processed)
        {
            ++callback_count;
            observed_kind = processed.interpreted.kind;
            observed_payload.assign(processed.udp_frame.udp_payload.begin(), processed.udp_frame.udp_payload.end());
            observed_source_ipv4_be = processed.udp_frame.source_ipv4_be;
            observed_dest_ipv4_be = processed.udp_frame.dest_ipv4_be;
            observed_source_port = processed.udp_frame.source_port;
            observed_dest_port = processed.udp_frame.dest_port;
            observed_queue_id = processed.source_queue_id;
            observed_ts_ns = processed.source_ts_ns;
        });

    assert(stats.accepted_packets == 1U);
    assert(callback_count == 1U);
    assert(observed_kind == rxtech::PacketKind::data_packet);
    assert(observed_payload.size() == 2048U);
    assert(observed_source_ipv4_be == source_ipv4_be);
    assert(observed_dest_ipv4_be == dest_ipv4_be);
    assert(observed_source_port == source_port);
    assert(observed_dest_port == dest_port);
    assert(observed_queue_id == queue_id);
    assert(observed_ts_ns == ts_ns);
    assert(stats.accepted_bytes == legacy_stats.accepted_bytes);
    assert(stats.filtered_packets == legacy_stats.filtered_packets);
    assert(observed_payload == legacy_payload);
    assert(observed_kind == legacy_kind);
    assert(observed_source_ipv4_be == legacy_source_ipv4_be);
    assert(observed_dest_ipv4_be == legacy_dest_ipv4_be);
    assert(observed_source_port == legacy_source_port);
    assert(observed_dest_port == legacy_dest_port);
    return 0;
}
