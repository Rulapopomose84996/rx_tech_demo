#include "packet_pipeline.h"

#include <memory>

#include "udp_datagram_pipeline.h"

namespace rxtech
{

    PacketPipeline::PacketPipeline(const RxConfig &config, const ProtocolSpec &spec)
        : datagram_pipeline_(std::make_unique<UdpDatagramPipeline>(config, spec))
    {
    }

    PacketPipeline::~PacketPipeline() = default;

    PacketProcessStats PacketPipeline::process_packet(const PacketDesc &packet, IMetricsCollector &metrics,
                                                      std::ostream *diagnostic_output, std::uint32_t &invalid_dumped,
                                                      const std::function<void(const ProcessedPacket &)> &on_packet)
    {
        PacketProcessStats stats;
        const auto udp_frames = assembler_.push(packet);
        for (const auto &udp_frame : udp_frames)
        {
            UdpDatagramDesc datagram;
            datagram.payload_data = udp_frame.udp_payload.data();
            datagram.payload_len = static_cast<std::uint32_t>(udp_frame.udp_payload.size());
            datagram.src_ipv4_be = udp_frame.source_ipv4_be;
            datagram.dst_ipv4_be = udp_frame.dest_ipv4_be;
            datagram.src_port = udp_frame.source_port;
            datagram.dst_port = udp_frame.dest_port;
            datagram.ts_ns = packet.ts_ns;
            datagram.queue_id = packet.queue_id;
            datagram.cookie = packet.cookie;
            datagram.has_global_sequence = udp_frame.has_global_sequence;
            datagram.global_sequence = udp_frame.global_sequence;

            const PacketProcessStats datagram_stats =
                datagram_pipeline_->process_datagram(datagram, metrics, diagnostic_output, invalid_dumped, on_packet);

            stats.accepted_bytes += datagram_stats.accepted_bytes;
            stats.accepted_packets += datagram_stats.accepted_packets;
            stats.filtered_packets += datagram_stats.filtered_packets;
        }

        return stats;
    }

} // namespace rxtech
