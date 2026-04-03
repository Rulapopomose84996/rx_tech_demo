#include "packet_pipeline.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

namespace rxtech
{
    namespace
    {

        std::uint32_t parse_ipv4_be(const std::string &ipv4)
        {
            std::uint32_t octets[4] = {0U, 0U, 0U, 0U};
            char dot1 = '\0';
            char dot2 = '\0';
            char dot3 = '\0';
            std::istringstream stream(ipv4);
            if (!(stream >> octets[0] >> dot1 >> octets[1] >> dot2 >> octets[2] >> dot3 >> octets[3]))
            {
                return 0U;
            }
            if (dot1 != '.' || dot2 != '.' || dot3 != '.')
            {
                return 0U;
            }
            for (std::uint32_t octet : octets)
            {
                if (octet > 255U)
                {
                    return 0U;
                }
            }
            return (octets[0] << 24U) | (octets[1] << 16U) | (octets[2] << 8U) | octets[3];
        }

        std::uint32_t read_u32_le_at(const std::uint8_t *data, std::size_t size, std::size_t offset)
        {
            if (data == nullptr || offset + 4U > size)
            {
                return 0U;
            }
            return static_cast<std::uint32_t>(data[offset + 0U]) |
                   (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
                   (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
                   (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
        }

        std::string hex_preview(const std::uint8_t *data, std::size_t size, std::size_t bytes_to_show)
        {
            std::ostringstream out;
            out << std::hex << std::setfill('0');
            const std::size_t count = std::min(size, bytes_to_show);
            for (std::size_t index = 0; index < count; ++index)
            {
                if (index != 0U)
                {
                    out << ' ';
                }
                out << std::setw(2) << static_cast<unsigned int>(data[index]);
            }
            return out.str();
        }

        void emit_invalid_packet_diagnostic(std::ostream &out,
                                            const PacketDesc &packet,
                                            const ParsedPacketView &parsed,
                                            RejectReason reason)
        {
            out << "[invalid-sample]"
                << " len=" << packet.len
                << " queue=" << packet.queue_id
                << " magic@0=0x" << std::hex << std::setw(8) << std::setfill('0') << read_u32_le_at(packet.data, packet.len, 0U)
                << std::dec
                << " reason=" << reject_reason_name(reason)
                << "\n";
            out << "[invalid-sample] decoded="
                << " kind=" << packet_kind_name(parsed.kind)
                << " cpi=" << parsed.cpi
                << " channel=" << parsed.channel
                << " prt=" << parsed.prt
                << " packet_index=" << parsed.packet_index
                << " tail=0x" << std::hex << std::setw(8) << std::setfill('0') << parsed.tail
                << std::dec
                << " payload_len=" << parsed.payload_len
                << " rx_tsc=" << parsed.rx_tsc
                << "\n";
            out << "[invalid-sample] preview=" << hex_preview(packet.data, packet.len, 64U) << "\n";
            out.flush();
        }

    } // namespace

    PacketPipeline::PacketPipeline(const RxConfig &config, const ProtocolSpec &spec)
        : config_(config),
          spec_(spec),
          parser_(spec),
          validator_(spec),
          interpreter_(spec),
          allowed_source_ipv4_be_(parse_ipv4_be(config.allowed_source_ipv4)),
          receiver_ipv4_be_(parse_ipv4_be(config.receiver_ipv4))
    {
    }

    bool PacketPipeline::matches_packet_filter(const UdpPayloadFrame &frame) const
    {
        const bool source_filter_enabled = !config_.allowed_source_ipv4.empty();
        const bool dest_ip_filter_enabled = !config_.receiver_ipv4.empty();
        const bool dest_port_filter_enabled = config_.allowed_dest_port != 0U;
        if (!source_filter_enabled && !dest_ip_filter_enabled && !dest_port_filter_enabled)
        {
            return true;
        }
        if (source_filter_enabled && frame.source_ipv4_be != allowed_source_ipv4_be_)
        {
            return false;
        }
        if (dest_ip_filter_enabled && frame.dest_ipv4_be != receiver_ipv4_be_)
        {
            return false;
        }
        if (dest_port_filter_enabled && frame.dest_port != static_cast<std::uint16_t>(config_.allowed_dest_port))
        {
            return false;
        }
        return true;
    }

    PacketProcessStats PacketPipeline::process_packet(const PacketDesc &packet,
                                                      IMetricsCollector &metrics,
                                                      std::ostream *diagnostic_output,
                                                      std::uint32_t &invalid_dumped,
                                                      const std::function<void(const ProcessedPacket &)> &on_packet)
    {
        PacketProcessStats stats;
        const auto udp_frames = assembler_.push(packet);
        for (const auto &udp_frame : udp_frames)
        {
            if (!matches_packet_filter(udp_frame))
            {
                ++stats.filtered_packets;
                continue;
            }

            stats.accepted_bytes += udp_frame.udp_payload.size();
            ++stats.accepted_packets;

            const ParsedPacketView parsed = parser_.parse(udp_frame);
            const PacketValidity validation = validator_.validate(parsed);
            if (!validation.ok)
            {
                metrics.on_reject(validation.reason);
                if (diagnostic_output != nullptr && invalid_dumped < 5U)
                {
                    PacketDesc diagnostic_packet;
                    diagnostic_packet.data = const_cast<std::uint8_t *>(udp_frame.udp_payload.data());
                    diagnostic_packet.len = static_cast<std::uint32_t>(udp_frame.udp_payload.size());
                    diagnostic_packet.queue_id = packet.queue_id;
                    emit_invalid_packet_diagnostic(*diagnostic_output, diagnostic_packet, parsed, validation.reason);
                    ++invalid_dumped;
                }
                continue;
            }

            const InterpretedPacketView interpreted = interpreter_.interpret(parsed);
            if (!interpreted.valid)
            {
                metrics.on_reject(interpreted.reject_reason);
                if (diagnostic_output != nullptr && invalid_dumped < 5U)
                {
                    PacketDesc diagnostic_packet;
                    diagnostic_packet.data = const_cast<std::uint8_t *>(udp_frame.udp_payload.data());
                    diagnostic_packet.len = static_cast<std::uint32_t>(udp_frame.udp_payload.size());
                    diagnostic_packet.queue_id = packet.queue_id;
                    emit_invalid_packet_diagnostic(*diagnostic_output, diagnostic_packet, parsed, interpreted.reject_reason);
                    ++invalid_dumped;
                }
                continue;
            }

            metrics.on_valid_packet(interpreted.kind);
            if (packet.ts_ns != 0U)
            {
                metrics.on_packet_latency_ns(packet.ts_ns);
            }

            ProcessedPacket processed;
            processed.udp_frame = udp_frame;
            processed.parsed = parsed;
            processed.interpreted = interpreted;
            processed.source_queue_id = packet.queue_id;
            processed.source_ts_ns = packet.ts_ns;
            on_packet(processed);
        }
        return stats;
    }

} // namespace rxtech
