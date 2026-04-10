#include "udp_datagram_pipeline.h"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

#include "rxtech/time_utils.h"

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
            for (const std::uint32_t octet : octets)
            {
                if (octet > 255U)
                {
                    return 0U;
                }
            }
            return (octets[0] << 24U) | (octets[1] << 16U) | (octets[2] << 8U) | octets[3];
        }

        bool is_malformed_descriptor(const UdpDatagramDesc &datagram)
        {
            return datagram.payload_len > 0U && datagram.payload_data == nullptr;
        }

#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
        constexpr std::uint64_t kRejectDiagnosticIntervalNs = 5ULL * 1000ULL * 1000ULL * 1000ULL;

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
            const std::size_t count = size < bytes_to_show ? size : bytes_to_show;
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

        void emit_invalid_packet_diagnostic(std::ostream &out, const UdpDatagramDesc &datagram,
                                            const ParsedPacketView &parsed, const RejectReason reason)
        {
            out << "[invalid-sample]"
                << " len=" << datagram.payload_len << " queue=" << datagram.queue_id << " magic@0=0x" << std::hex
                << std::setw(8) << std::setfill('0') << read_u32_le_at(datagram.payload_data, datagram.payload_len, 0U)
                << std::dec << " reason=" << reject_reason_name(reason) << "\n";
            out << "[invalid-sample] decoded="
                << " kind=" << packet_kind_name(parsed.kind) << " cpi=" << parsed.cpi << " channel=" << parsed.channel
                << " prt=" << parsed.prt << " packet_index=" << parsed.packet_index << " tail=0x" << std::hex
                << std::setw(8) << std::setfill('0') << parsed.tail << std::dec << " payload_len=" << parsed.payload_len
                << " rx_tsc=" << parsed.rx_tsc << "\n";
            out << "[invalid-sample] preview=" << hex_preview(datagram.payload_data, datagram.payload_len, 64U) << "\n";
            out.flush();
        }

        void emit_invalid_packet_summary(std::ostream &out, RejectReason reason, std::uint64_t suppressed_count,
                                         std::uint64_t total_count)
        {
            out << "[invalid-sample-rate-limit]"
                << " reason=" << reject_reason_name(reason) << " suppressed=" << suppressed_count
                << " total=" << total_count << "\n";
            out.flush();
        }
#endif

    } // namespace

    UdpDatagramPipeline::UdpDatagramPipeline(const RxConfig &config, const ProtocolSpec &spec)
        : config_(config), spec_(spec), parser_(spec), validator_(spec), interpreter_(spec),
          allowed_source_ipv4_be_(parse_ipv4_be(config.allowed_source_ipv4)),
          receiver_ipv4_be_(parse_ipv4_be(config.receiver_ipv4))
    {
    }

    bool UdpDatagramPipeline::matches_packet_filter(const UdpPayloadFrame &frame) const
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

#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
    void UdpDatagramPipeline::maybe_emit_invalid_diagnostic(std::ostream &diagnostic_output,
                                                            const UdpDatagramDesc &datagram,
                                                            const ParsedPacketView &parsed, RejectReason reason,
                                                            std::uint32_t &invalid_dumped)
    {
        RejectDiagnosticState &state = reject_diagnostic_states_[static_cast<std::size_t>(reason)];
        ++state.total_count;

        const std::uint64_t now_ns = steady_clock_now_ns();
        if (!state.emitted_once)
        {
            emit_invalid_packet_diagnostic(diagnostic_output, datagram, parsed, reason);
            ++invalid_dumped;
            state.emitted_once = true;
            state.next_emit_after_ns = now_ns + kRejectDiagnosticIntervalNs;
            return;
        }

        if (now_ns >= state.next_emit_after_ns)
        {
            if (state.suppressed_count > 0U)
            {
                emit_invalid_packet_summary(diagnostic_output, reason, state.suppressed_count, state.total_count);
            }
            emit_invalid_packet_diagnostic(diagnostic_output, datagram, parsed, reason);
            ++invalid_dumped;
            state.suppressed_count = 0;
            state.next_emit_after_ns = now_ns + kRejectDiagnosticIntervalNs;
            return;
        }

        ++state.suppressed_count;
    }
#endif

    PacketProcessStats
    UdpDatagramPipeline::process_datagram(const UdpDatagramDesc &datagram, IMetricsCollector &metrics,
                                          std::ostream *diagnostic_output, std::uint32_t &invalid_dumped,
                                          const std::function<void(const ProcessedPacket &)> &on_packet)
    {
        PacketProcessStats stats;
        if (is_malformed_descriptor(datagram))
        {
            metrics.on_error();
            return stats;
        }

        if (datagram.truncated)
        {
            metrics.on_reject(RejectReason::truncated_datagram);
            return stats;
        }

        UdpPayloadFrame udp_frame;
        if (datagram.payload_data != nullptr && datagram.payload_len != 0U)
        {
            udp_frame.udp_payload.set_view(datagram.payload_data, datagram.payload_len);
        }
        udp_frame.source_ipv4_be = datagram.src_ipv4_be;
        udp_frame.dest_ipv4_be = datagram.dst_ipv4_be;
        udp_frame.source_port = datagram.src_port;
        udp_frame.dest_port = datagram.dst_port;

        if (!matches_packet_filter(udp_frame))
        {
            ++stats.filtered_packets;
            return stats;
        }

        stats.accepted_bytes += udp_frame.udp_payload.size();
        ++stats.accepted_packets;

        const ParsedPacketView parsed = parser_.parse(udp_frame);
        const PacketValidity validation = validator_.validate(parsed);
        if (!validation.ok)
        {
            metrics.on_reject(validation.reason);
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
            if (diagnostic_output != nullptr)
            {
                maybe_emit_invalid_diagnostic(*diagnostic_output, datagram, parsed, validation.reason, invalid_dumped);
            }
#else
            (void)diagnostic_output;
            (void)invalid_dumped;
#endif
            return stats;
        }

        const InterpretedPacketView interpreted = interpreter_.interpret(parsed);
        if (!interpreted.valid)
        {
            metrics.on_reject(interpreted.reject_reason);
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
            if (diagnostic_output != nullptr)
            {
                maybe_emit_invalid_diagnostic(*diagnostic_output, datagram, parsed, interpreted.reject_reason,
                                              invalid_dumped);
            }
#endif
            return stats;
        }

        metrics.on_valid_packet(interpreted.kind);
        if (datagram.ts_ns != 0U)
        {
            metrics.on_packet_latency_ns(datagram.ts_ns);
        }

        ProcessedPacket processed;
        processed.udp_frame = std::move(udp_frame);
        processed.parsed = parsed;
        processed.interpreted = interpreted;
        processed.source_queue_id = datagram.queue_id;
        processed.source_ts_ns = datagram.ts_ns;
        on_packet(processed);
        return stats;
    }

} // namespace rxtech
