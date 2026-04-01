#include "rxtech/owner_loop.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "rxtech/sample_packet_parser.h"
#include "rxtech/sample_packet_validator.h"

namespace rxtech {

namespace {

struct CapturedPacket {
    std::vector<std::uint8_t> payload;
    std::uint64_t ts_ns = 0;
    std::uint32_t port_id = 0;
    std::uint32_t queue_id = 0;
    std::uint32_t face_id = 0;
    std::uint16_t cpi = 0;
    std::uint16_t channel = 0;
    std::uint16_t prt = 0;
    std::uint16_t packet_index = 0;
    std::string packet_kind;
    std::string validation;
};

void merge_backend_stats(RunSummary& summary, const BackendStats& backend_stats) {
    summary.rx_packets = backend_stats.rx_packets != 0U ? backend_stats.rx_packets : summary.rx_packets;
    summary.rx_bytes = backend_stats.rx_bytes != 0U ? backend_stats.rx_bytes : summary.rx_bytes;
    summary.dropped_packets += backend_stats.backend_drops;
    summary.backend_errors += backend_stats.rx_errors;
    summary.rx_polls = backend_stats.rx_polls;
    summary.empty_polls = backend_stats.empty_polls;
    summary.queue_id = backend_stats.queue_id;
    summary.xdp_prog_id = backend_stats.xdp_prog_id;
    summary.xsk_bind_flags = backend_stats.xsk_bind_flags;
    summary.umem_size = backend_stats.umem_size;
    summary.frame_size = backend_stats.frame_size;
    summary.fill_ring_size = backend_stats.fill_ring_size;
    summary.completion_ring_size = backend_stats.completion_ring_size;
    summary.xdp_attach_mode = backend_stats.xdp_attach_mode;
    summary.xsk_mode = backend_stats.xsk_mode;
    if (summary.rx_polls > 0U) {
        summary.empty_poll_ratio = static_cast<double>(summary.empty_polls) / static_cast<double>(summary.rx_polls);
    }
}

double calculate_drop_rate(const RunSummary& summary) {
    const double total = static_cast<double>(summary.rx_packets + summary.dropped_packets);
    if (total <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(summary.dropped_packets) / total;
}

void print_status_snapshot(std::ostream& out,
                           const RunSummary& summary,
                           const std::chrono::steady_clock::duration& elapsed) {
    const auto elapsed_seconds = std::max<std::uint64_t>(
        1ULL,
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()));
    const double aggregate_gbps =
        (static_cast<double>(summary.rx_bytes) * 8.0) / static_cast<double>(elapsed_seconds) / 1'000'000'000.0;

    out << "[status] elapsed=" << elapsed_seconds << "s"
        << " rx_packets=" << summary.rx_packets
        << " rx_bytes=" << summary.rx_bytes
        << " parsed_packets=" << summary.parsed_packets
        << " control_table_packets=" << summary.control_table_packets
        << " data_packets=" << summary.data_packets
        << " gbps=" << aggregate_gbps
        << " drop_rate=" << calculate_drop_rate(summary)
        << " empty_poll_ratio=" << summary.empty_poll_ratio
        << '\n';
    out.flush();
}

void send_feedback_snapshot(const RxConfig& config, const RunSummary& summary, std::ostream* status_output) {
#ifdef __linux__
    if (!config.feedback_enabled || config.feedback_host.empty() || config.feedback_port == 0U) {
        return;
    }

    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        if (status_output != nullptr) {
            *status_output << "[feedback] socket_error errno=" << errno << "\n";
            status_output->flush();
        }
        return;
    }

    if (!config.feedback_bind_host.empty()) {
        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = 0;
        if (inet_pton(AF_INET, config.feedback_bind_host.c_str(), &bind_addr.sin_addr) == 1) {
            if (bind(fd, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0 && status_output != nullptr) {
                *status_output << "[feedback] bind_error errno=" << errno << " source=" << config.feedback_bind_host << "\n";
                status_output->flush();
            }
        }
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(config.feedback_port));
    if (inet_pton(AF_INET, config.feedback_host.c_str(), &addr.sin_addr) != 1) {
        if (status_output != nullptr) {
            *status_output << "[feedback] invalid_target=" << config.feedback_host << ":" << config.feedback_port << "\n";
            status_output->flush();
        }
        close(fd);
        return;
    }

    std::ostringstream payload;
    payload << "{\"type\":\"receiver_feedback\""
            << ",\"rx_packets\":" << summary.rx_packets
            << ",\"rx_bytes\":" << summary.rx_bytes
            << ",\"parsed_packets\":" << summary.parsed_packets
            << ",\"control_table_packets\":" << summary.control_table_packets
            << ",\"data_packets\":" << summary.data_packets
            << ",\"loss_rate\":" << calculate_drop_rate(summary)
            << ",\"queue_id\":" << summary.queue_id
            << ",\"gbps\":" << summary.actual_rx_gbps
            << "}";

    const std::string message = payload.str();
    const ssize_t sent = sendto(fd, message.data(), message.size(), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (sent < 0 && status_output != nullptr) {
        *status_output << "[feedback] send_error errno=" << errno << " target=" << config.feedback_host << ":" << config.feedback_port << "\n";
        status_output->flush();
    }
    close(fd);
#else
    (void)config;
    (void)summary;
    (void)status_output;
#endif
}

void write_capture_index_header(std::ostream& index_stream) {
    index_stream << "sequence,offset,length,ts_ns,port_id,queue_id,face_id,cpi,channel,prt,packet_index,packet_kind,validation\n";
}

std::uint32_t read_u32_le_at(const std::uint8_t* data, std::size_t size, std::size_t offset) {
    if (data == nullptr || offset + 4U > size) {
        return 0U;
    }
    return static_cast<std::uint32_t>(data[offset + 0U]) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

std::string hex_preview(const std::uint8_t* data, std::size_t size, std::size_t bytes_to_show) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    const std::size_t count = std::min(size, bytes_to_show);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0U) {
            out << ' ';
        }
        out << std::setw(2) << static_cast<unsigned int>(data[index]);
    }
    return out.str();
}

void emit_invalid_packet_diagnostic(std::ostream& out,
                                    const PacketDesc& packet,
                                    const SamplePacketView& parsed,
                                    const SamplePacketValidation& validation) {
    out << "[invalid-sample]"
        << " len=" << packet.len
        << " queue=" << packet.queue_id
        << " header_offset=" << parsed.header_offset
        << " magic@0=0x" << std::hex << std::setw(8) << std::setfill('0') << read_u32_le_at(packet.data, packet.len, 0U)
        << " magic@offset=0x" << std::hex << std::setw(8) << std::setfill('0')
        << read_u32_le_at(packet.data, packet.len, parsed.header_offset)
        << std::dec
        << " reason=" << validation.reason
        << "\n";
    out << "[invalid-sample] preview=" << hex_preview(packet.data, packet.len, 64U) << "\n";
    out.flush();
}

}  // namespace

void OwnerLoop::set_status_output(std::ostream* output) {
    status_output_ = output;
}

RunSummary OwnerLoop::run(ReceiveContext& context,
                          CaptureArtifacts& artifacts,
                          const std::function<bool()>& should_stop) const {
    if (artifacts.packet_stream == nullptr || artifacts.index_stream == nullptr) {
        throw std::runtime_error("capture artifacts are incomplete");
    }

    SamplePacketParser parser;
    SamplePacketValidator validator;
    write_capture_index_header(*artifacts.index_stream);

    const auto start_time = std::chrono::steady_clock::now();
    const auto status_interval = std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.status_interval_seconds));
    const auto feedback_interval = std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.feedback_interval_seconds));
    auto next_status_at = start_time + status_interval;
    auto next_feedback_at = start_time + feedback_interval;
    std::string run_status = "success";
    std::string run_error;
    std::uint32_t invalid_dumped = 0;

    while (!should_stop()) {
        RxBurst burst;
        if (!context.backend->recv_burst(burst, context.config.max_burst)) {
            run_status = "error";
            run_error = "backend recv_burst failed";
            context.backend->release_burst(burst);
            break;
        }

        std::uint64_t burst_bytes = 0;
        for (const PacketDesc& packet : burst.packets) {
            burst_bytes += packet.len;
            const SamplePacketView parsed = parser.parse(packet);
            const SamplePacketValidation validation = validator.validate(parsed);
            if (!validation.ok) {
                context.metrics->on_drop();
                context.metrics->on_invalid_header(packet.port_id);
                if (status_output_ != nullptr && invalid_dumped < 5U) {
                    emit_invalid_packet_diagnostic(*status_output_, packet, parsed, validation);
                    ++invalid_dumped;
                }
                continue;
            }

            context.metrics->on_parsed_packet();
            context.metrics->on_port_packet(packet.port_id, packet.len);
            if (packet.ts_ns != 0U) {
                context.metrics->on_packet_latency_ns(packet.ts_ns);
            }
            if (parsed.kind == SamplePacketKind::control_table) {
                context.metrics->on_control_table_packet();
            } else if (parsed.kind == SamplePacketKind::data_packet) {
                context.metrics->on_data_packet();
            }

            CapturedPacket captured;
            captured.payload.assign(packet.data, packet.data + packet.len);
            captured.ts_ns = packet.ts_ns;
            captured.port_id = packet.port_id;
            captured.queue_id = packet.queue_id;
            captured.face_id = packet.face_id;
            captured.cpi = parsed.cpi;
            captured.channel = parsed.channel;
            captured.prt = parsed.prt;
            captured.packet_index = parsed.packet_index;
            captured.packet_kind = sample_packet_kind_name(parsed.kind);
            captured.validation = "ok";

            artifacts.packet_stream->write(reinterpret_cast<const char*>(captured.payload.data()),
                                           static_cast<std::streamsize>(captured.payload.size()));
            *artifacts.index_stream << artifacts.recorded_packets
                                    << ',' << artifacts.file_offset
                                    << ',' << captured.payload.size()
                                    << ',' << captured.ts_ns
                                    << ',' << captured.port_id
                                    << ',' << captured.queue_id
                                    << ',' << captured.face_id
                                    << ',' << captured.cpi
                                    << ',' << captured.channel
                                    << ',' << captured.prt
                                    << ',' << captured.packet_index
                                    << ',' << captured.packet_kind
                                    << ',' << captured.validation
                                    << '\n';

            artifacts.file_offset += captured.payload.size();
            artifacts.recorded_bytes += captured.payload.size();
            ++artifacts.recorded_packets;
            artifacts.captured_bytes += captured.payload.size();
            ++artifacts.captured_packets;
        }

        if (!burst.packets.empty()) {
            context.metrics->on_burst(burst.packets.size(), burst_bytes);
        }
        context.backend->release_burst(burst);

        const auto now = std::chrono::steady_clock::now();
        if (context.config.run_until_stopped && ((status_output_ != nullptr && now >= next_status_at) ||
                                                 (context.config.feedback_enabled && now >= next_feedback_at))) {
            RunSummary status_summary =
                context.metrics->finalize(context.backend->name(),
                                          "light_parse",
                                          "sample_replay",
                                          std::max<std::uint32_t>(
                                              1U,
                                              static_cast<std::uint32_t>(
                                                  std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count())));
            status_summary.captured_packets = artifacts.captured_packets;
            status_summary.captured_bytes = artifacts.captured_bytes;
            status_summary.recorded_packets = artifacts.recorded_packets;
            status_summary.recorded_bytes = artifacts.recorded_bytes;
            merge_backend_stats(status_summary, context.backend->stats());
            if (status_output_ != nullptr && now >= next_status_at) {
                print_status_snapshot(*status_output_, status_summary, now - start_time);
                next_status_at = now + status_interval;
            }
            if (context.config.feedback_enabled && now >= next_feedback_at) {
                send_feedback_snapshot(context.config, status_summary, status_output_);
                next_feedback_at = now + feedback_interval;
            }
        }
    }

    artifacts.packet_stream->flush();
    artifacts.index_stream->flush();

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed_seconds = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()));

    RunSummary summary = context.metrics->finalize(context.backend->name(), "light_parse", "sample_replay", elapsed_seconds);
    summary.run_status = run_status;
    summary.error_message = run_error;
    summary.backend_available = true;
    summary.backend_status = "available";
    summary.captured_packets = artifacts.captured_packets;
    summary.captured_bytes = artifacts.captured_bytes;
    summary.recorded_packets = artifacts.recorded_packets;
    summary.recorded_bytes = artifacts.recorded_bytes;
    merge_backend_stats(summary, context.backend->stats());

    if (context.config.run_until_stopped && status_output_ != nullptr) {
        print_status_snapshot(*status_output_, summary, end_time - start_time);
    }
    send_feedback_snapshot(context.config, summary, status_output_);

    return summary;
}

}  // namespace rxtech
