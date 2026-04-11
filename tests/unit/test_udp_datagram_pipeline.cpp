#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
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

    constexpr std::uint32_t kSourceIpv4Be = 0xac140bdeU;
    constexpr std::uint32_t kDestIpv4Be = 0xac140b64U;
    constexpr std::uint16_t kSourcePort = 0xe479U;
    constexpr std::uint16_t kDestPort = 0x270fU;
    constexpr std::uint64_t kTimestampNs = 123456U;
    constexpr std::uint32_t kQueueId = 7U;
    constexpr std::uintptr_t kCookie = 0x1234U;

    struct CallbackObservation
    {
        std::size_t count = 0U;
        rxtech::PacketKind kind = rxtech::PacketKind::unknown;
        std::vector<std::uint8_t> payload;
        std::uint32_t source_ipv4_be = 0U;
        std::uint32_t dest_ipv4_be = 0U;
        std::uint16_t source_port = 0U;
        std::uint16_t dest_port = 0U;
        std::uint32_t queue_id = 0U;
        std::uint64_t ts_ns = 0U;
    };

    struct PipelineRunResult
    {
        rxtech::PacketProcessStats stats;
        rxtech::RunSummary summary;
        CallbackObservation callback;
        std::uint32_t invalid_dumped = 0U;
    };

    std::vector<std::uint8_t> make_valid_payload()
    {
        std::vector<std::uint8_t> payload = {0x03, 0xff, 0xaa, 0x55, 0x01, 0x00, 0x00, 0x00,
                                             0x22, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
        payload.resize(2048U, 0xABU);
        return payload;
    }

    std::vector<std::uint8_t> make_invalid_payload()
    {
        return {0x03U, 0xFFU, 0xAAU, 0x55U};
    }

    std::vector<std::uint8_t> make_udp_frame_with_payload(const std::vector<std::uint8_t> &payload)
    {
        const std::uint16_t udp_length = static_cast<std::uint16_t>(8U + payload.size());
        const std::uint16_t ip_total_length = static_cast<std::uint16_t>(20U + udp_length);
        std::vector<std::uint8_t> bytes = {0x9c,
                                           0x47,
                                           0x82,
                                           0xe1,
                                           0x36,
                                           0xd0,
                                           0x9c,
                                           0x47,
                                           0x82,
                                           0xe1,
                                           0x36,
                                           0xdc,
                                           0x08,
                                           0x00,
                                           0x45,
                                           0x00,
                                           static_cast<std::uint8_t>((ip_total_length >> 8U) & 0xffU),
                                           static_cast<std::uint8_t>(ip_total_length & 0xffU),
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x40,
                                           0x11,
                                           0x00,
                                           0x00,
                                           0xac,
                                           0x14,
                                           0x0b,
                                           0xde,
                                           0xac,
                                           0x14,
                                           0x0b,
                                           0x64,
                                           0xe4,
                                           0x79,
                                           0x27,
                                           0x0f,
                                           static_cast<std::uint8_t>((udp_length >> 8U) & 0xffU),
                                           static_cast<std::uint8_t>(udp_length & 0xffU),
                                           0x00,
                                           0x00};
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    }

    void record_callback(CallbackObservation &observation, const rxtech::ProcessedPacket &processed)
    {
        ++observation.count;
        observation.kind = processed.interpreted.kind;
        observation.payload.assign(processed.udp_frame.udp_payload.begin(), processed.udp_frame.udp_payload.end());
        observation.source_ipv4_be = processed.udp_frame.source_ipv4_be;
        observation.dest_ipv4_be = processed.udp_frame.dest_ipv4_be;
        observation.source_port = processed.udp_frame.source_port;
        observation.dest_port = processed.udp_frame.dest_port;
        observation.queue_id = processed.source_queue_id;
        observation.ts_ns = processed.source_ts_ns;
    }

    PipelineRunResult run_legacy(const rxtech::RxConfig &config, const std::vector<std::uint8_t> &frame)
    {
        const rxtech::ProtocolSpec spec = rxtech::protocol_spec_from_config(config);
        rxtech::PacketPipeline pipeline(config, spec);
        rxtech::MetricsCollector metrics;

        rxtech::PacketDesc packet;
        packet.data = const_cast<std::uint8_t *>(frame.data());
        packet.len = static_cast<std::uint32_t>(frame.size());
        packet.ts_ns = kTimestampNs;
        packet.queue_id = kQueueId;
        packet.cookie = kCookie;

        PipelineRunResult result;
        result.stats = pipeline.process_packet(packet, metrics, nullptr, result.invalid_dumped,
                                               [&](const rxtech::ProcessedPacket &processed)
                                               { record_callback(result.callback, processed); });
        result.summary = metrics.finalize("legacy", "unit", "udp_datagram_pipeline", 1U);
        return result;
    }

    PipelineRunResult run_datagram(const rxtech::RxConfig &config, const rxtech::UdpDatagramDesc &datagram)
    {
        const rxtech::ProtocolSpec spec = rxtech::protocol_spec_from_config(config);
        rxtech::UdpDatagramPipeline pipeline(config, spec);
        rxtech::MetricsCollector metrics;

        PipelineRunResult result;
        result.stats = pipeline.process_datagram(datagram, metrics, nullptr, result.invalid_dumped,
                                                 [&](const rxtech::ProcessedPacket &processed)
                                                 { record_callback(result.callback, processed); });
        result.summary = metrics.finalize("datagram", "unit", "udp_datagram_pipeline", 1U);
        return result;
    }

    rxtech::UdpDatagramDesc make_datagram(const std::vector<std::uint8_t> &payload)
    {
        rxtech::UdpDatagramDesc datagram;
        datagram.payload_data = payload.data();
        datagram.payload_len = static_cast<std::uint32_t>(payload.size());
        datagram.src_ipv4_be = kSourceIpv4Be;
        datagram.dst_ipv4_be = kDestIpv4Be;
        datagram.src_port = kSourcePort;
        datagram.dst_port = kDestPort;
        datagram.ts_ns = kTimestampNs;
        datagram.queue_id = kQueueId;
        datagram.cookie = kCookie;
        datagram.backend_kind = rxtech::BackendKind::socket;
        return datagram;
    }

    void assert_callback_parity(const CallbackObservation &actual, const CallbackObservation &expected)
    {
        assert(actual.count == expected.count);
        assert(actual.kind == expected.kind);
        assert(actual.payload == expected.payload);
        assert(actual.source_ipv4_be == expected.source_ipv4_be);
        assert(actual.dest_ipv4_be == expected.dest_ipv4_be);
        assert(actual.source_port == expected.source_port);
        assert(actual.dest_port == expected.dest_port);
        assert(actual.queue_id == expected.queue_id);
        assert(actual.ts_ns == expected.ts_ns);
    }

} // namespace

int main()
{
    {
        const std::vector<std::uint8_t> payload = make_valid_payload();
        const std::vector<std::uint8_t> frame = make_udp_frame_with_payload(payload);
        const PipelineRunResult legacy = run_legacy(rxtech::load_default_config(), frame);
        const PipelineRunResult datagram = run_datagram(rxtech::load_default_config(), make_datagram(payload));

        assert(legacy.stats.accepted_packets == 1U);
        assert(datagram.stats.accepted_packets == 1U);
        assert(legacy.callback.count == 1U);
        assert(datagram.callback.count == 1U);
        assert(legacy.callback.kind == rxtech::PacketKind::data_packet);
        assert(datagram.callback.kind == rxtech::PacketKind::data_packet);
        assert(legacy.callback.payload.size() == 2048U);
        assert(datagram.callback.payload.size() == 2048U);
        assert_callback_parity(datagram.callback, legacy.callback);
        assert(datagram.stats.accepted_bytes == legacy.stats.accepted_bytes);
        assert(datagram.stats.filtered_packets == legacy.stats.filtered_packets);
    }

    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.ingress.allowed_source_ipv4 = "172.20.11.1";
        const std::vector<std::uint8_t> payload = make_valid_payload();
        const std::vector<std::uint8_t> frame = make_udp_frame_with_payload(payload);
        const PipelineRunResult legacy = run_legacy(config, frame);
        const PipelineRunResult datagram = run_datagram(config, make_datagram(payload));

        assert(legacy.stats.accepted_packets == 0U);
        assert(datagram.stats.accepted_packets == 0U);
        assert(legacy.stats.accepted_bytes == 0U);
        assert(datagram.stats.accepted_bytes == 0U);
        assert(legacy.stats.filtered_packets == 1U);
        assert(datagram.stats.filtered_packets == 1U);
        assert(legacy.callback.count == 0U);
        assert(datagram.callback.count == 0U);
        assert(datagram.summary.protocol.dropped_packets == legacy.summary.protocol.dropped_packets);
        assert(datagram.summary.backend.errors == legacy.summary.backend.errors);
    }

    {
        const std::vector<std::uint8_t> payload = make_invalid_payload();
        const std::vector<std::uint8_t> frame = make_udp_frame_with_payload(payload);
        const PipelineRunResult legacy = run_legacy(rxtech::load_default_config(), frame);
        const PipelineRunResult datagram = run_datagram(rxtech::load_default_config(), make_datagram(payload));

        assert(legacy.stats.accepted_packets == 1U);
        assert(datagram.stats.accepted_packets == 1U);
        assert(legacy.stats.accepted_bytes == payload.size());
        assert(datagram.stats.accepted_bytes == payload.size());
        assert(legacy.callback.count == 0U);
        assert(datagram.callback.count == 0U);
        assert(legacy.summary.protocol.dropped_packets == 1U);
        assert(datagram.summary.protocol.dropped_packets == 1U);
        assert(legacy.summary.reject_by_reason[static_cast<std::size_t>(rxtech::RejectReason::invalid_len)] == 1U);
        assert(datagram.summary.reject_by_reason[static_cast<std::size_t>(rxtech::RejectReason::invalid_len)] == 1U);
        assert(datagram.summary.backend.errors == legacy.summary.backend.errors);
    }

    {
        rxtech::UdpDatagramDesc malformed;
        malformed.payload_len = 16U;
        malformed.src_ipv4_be = kSourceIpv4Be;
        malformed.dst_ipv4_be = kDestIpv4Be;
        malformed.src_port = kSourcePort;
        malformed.dst_port = kDestPort;
        malformed.ts_ns = kTimestampNs;
        malformed.queue_id = kQueueId;
        malformed.cookie = kCookie;
        malformed.backend_kind = rxtech::BackendKind::socket;

        const PipelineRunResult datagram = run_datagram(rxtech::load_default_config(), malformed);
        const std::array<std::uint64_t, rxtech::kRejectReasonCount> empty_rejects{};
        assert(datagram.stats.accepted_packets == 0U);
        assert(datagram.stats.accepted_bytes == 0U);
        assert(datagram.stats.filtered_packets == 0U);
        assert(datagram.callback.count == 0U);
        assert(datagram.summary.backend.errors == 1U);
        assert(datagram.summary.protocol.dropped_packets == 0U);
        assert(datagram.summary.reject_by_reason == empty_rejects);
    }

    {
        const std::vector<std::uint8_t> payload = make_valid_payload();
        rxtech::UdpDatagramDesc datagram = make_datagram(payload);
        datagram.truncated = true;

        const PipelineRunResult result = run_datagram(rxtech::load_default_config(), datagram);
        assert(result.stats.accepted_packets == 0U);
        assert(result.stats.accepted_bytes == 0U);
        assert(result.stats.filtered_packets == 0U);
        assert(result.callback.count == 0U);
        assert(result.summary.backend.errors == 0U);
        assert(result.summary.protocol.dropped_packets == 1U);
        assert(result.summary.reject_by_reason[static_cast<std::size_t>(rxtech::RejectReason::truncated_datagram)] ==
               1U);
    }

    return 0;
}
