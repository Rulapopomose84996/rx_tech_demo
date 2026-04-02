#include "rxtech/owner_loop.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <unordered_set>
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
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/udp_payload_assembler.h"

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

struct ProtocolCpiStats {
    std::uint64_t control_table_packets = 0;
    std::uint64_t data_packets = 0;
    std::unordered_set<std::uint16_t> prts;
    std::unordered_set<std::uint16_t> channels;
};

struct ProtocolChannelStats {
    std::uint64_t data_packets = 0;
    std::uint64_t iq_count = 0;
    std::uint64_t data_bytes = 0;
    std::uint64_t zero_padding_bytes = 0;
};

using ProtocolPrtCoverage = std::map<std::uint16_t, std::unordered_set<std::uint16_t>>;

void merge_backend_stats(RunSummary& summary, const BackendStats& backend_stats) {
    summary.raw_rx_packets = backend_stats.rx_packets;
    summary.raw_rx_bytes = backend_stats.rx_bytes;
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
        << " raw_rx_packets=" << summary.raw_rx_packets
        << " filtered_packets=" << summary.filtered_packets
        << " rx_bytes=" << summary.rx_bytes
        << " parsed_packets=" << summary.parsed_packets
        << " control_table_packets=" << summary.control_table_packets
        << " data_packets=" << summary.data_packets
        << " packet_count=" << summary.packet_count
        << " cpi_count=" << summary.cpi_count
        << " prt_count=" << summary.prt_count
        << " channel_count=" << summary.channel_count
        << " gbps=" << aggregate_gbps
        << " drop_rate=" << calculate_drop_rate(summary)
        << " empty_poll_ratio=" << summary.empty_poll_ratio
        << '\n';
    out.flush();
}

std::uint32_t parse_ipv4_be(const std::string& ipv4) {
    std::uint32_t octets[4] = {0U, 0U, 0U, 0U};
    char dot1 = '\0';
    char dot2 = '\0';
    char dot3 = '\0';
    std::istringstream stream(ipv4);
    if (!(stream >> octets[0] >> dot1 >> octets[1] >> dot2 >> octets[2] >> dot3 >> octets[3])) {
        return 0U;
    }
    if (dot1 != '.' || dot2 != '.' || dot3 != '.') {
        return 0U;
    }
    for (std::uint32_t octet : octets) {
        if (octet > 255U) {
            return 0U;
        }
    }
    return (octets[0] << 24U) | (octets[1] << 16U) | (octets[2] << 8U) | octets[3];
}

bool matches_packet_filter(const RxConfig& config, const SamplePacketView& parsed) {
    const bool source_filter_enabled = !config.allowed_source_ipv4.empty();
    const bool dest_ip_filter_enabled = !config.receiver_ipv4.empty();
    const bool dest_port_filter_enabled = config.allowed_dest_port != 0U;
    if (!source_filter_enabled && !dest_ip_filter_enabled && !dest_port_filter_enabled) {
        return true;
    }
    if (!parsed.is_ipv4_udp) {
        return false;
    }
    if (source_filter_enabled && parsed.source_ipv4_be != parse_ipv4_be(config.allowed_source_ipv4)) {
        return false;
    }
    if (dest_ip_filter_enabled && parsed.dest_ipv4_be != parse_ipv4_be(config.receiver_ipv4)) {
        return false;
    }
    if (dest_port_filter_enabled && parsed.dest_port != static_cast<std::uint16_t>(config.allowed_dest_port)) {
        return false;
    }
    return true;
}

const char* protocol_channel_name(std::uint16_t channel) {
    switch (channel) {
        case 0:
            return "和路";
        case 1:
            return "俯仰差";
        case 2:
            return "方位差";
        case 3:
            return "辅助通道";
        default:
            return "未知通道";
    }
}

std::string build_human_summary(const RunSummary& summary) {
    std::ostringstream out;
    out << "\n========== 接收结束汇总 ==========\n";
    out << "运行结果： " << (summary.run_status == "success" ? "成功" : "失败") << "\n";
    out << "后端类型： " << summary.backend << "\n";
    out << "接收队列： " << summary.queue_id << "\n";
    out << "原始收包： " << summary.raw_rx_packets << " 包，" << summary.raw_rx_bytes << " 字节\n";
    out << "过滤丢弃： " << summary.filtered_packets << " 包\n";
    out << "候选业务包： " << summary.rx_packets << " 包，" << summary.rx_bytes << " 字节\n";
    out << "解析有效包： " << summary.parsed_packets << " 包\n";
    out << "控制表包： " << summary.control_table_packets << " 包\n";
    out << "数据包： " << summary.data_packets << " 包\n";
    out << "协议丢弃： " << summary.dropped_packets << " 包\n";
    out << "CPI 数： " << summary.cpi_count << "\n";
    out << "PRT 数： " << summary.prt_count << "\n";
    out << "完整 PRT 数： " << summary.complete_prt_count << "\n";
    out << "通道数： " << summary.channel_count << "\n";
    out << "最终包尾数量： " << summary.final_tail_packets << "\n";
    out << "已落盘包数： " << summary.packet_count << "\n";
    out << "抓包索引： " << summary.capture_index_path << "\n";
    out << "抓包数据： " << summary.capture_packets_path << "\n";
    if (!summary.protocol_channels.empty()) {
        out << "通道分布：\n";
        for (const auto& channel : summary.protocol_channels) {
            out << "  - 通道 " << channel.channel << "（" << channel.channel_name << "）："
                << channel.data_packets << " 个数据包，"
                << channel.iq_count << " 个 IQ，"
                << channel.data_bytes << " 字节数据，"
                << channel.zero_padding_bytes << " 字节补零\n";
        }
    }
    if (!summary.protocol_cpis.empty()) {
        out << "CPI 分布：\n";
        for (const auto& cpi : summary.protocol_cpis) {
            out << "  - CPI " << cpi.cpi
                << "：控制表 " << cpi.control_table_packets
                << " 包，数据包 " << cpi.data_packets
                << " 包，PRT 数 " << cpi.prt_count
                << "，通道数 " << cpi.channel_count << "\n";
        }
    }
    out << "==================================\n";
    return out.str();
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
    ProtocolSequenceInterpreter sequence_interpreter;
    UdpPayloadAssembler payload_assembler;
    write_capture_index_header(*artifacts.index_stream);

    const auto start_time = std::chrono::steady_clock::now();
    const auto status_interval = std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.status_interval_seconds));
    const auto feedback_interval = std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.feedback_interval_seconds));
    auto next_status_at = start_time + status_interval;
    auto next_feedback_at = start_time + feedback_interval;
    std::string run_status = "success";
    std::string run_error;
    std::uint32_t invalid_dumped = 0;
    std::unordered_set<std::uint16_t> unique_cpis;
    std::unordered_set<std::uint16_t> unique_prts;
    std::unordered_set<std::uint16_t> unique_channels;
    std::uint64_t filtered_packets = 0;
    std::map<std::uint16_t, ProtocolChannelStats> channel_stats;
    std::map<std::uint64_t, ProtocolCpiStats> cpi_stats;
    std::map<std::pair<std::uint64_t, std::uint16_t>, ProtocolPrtCoverage> prt_coverage;
    std::uint64_t final_tail_packets = 0;

    while (!should_stop()) {
        RxBurst burst;
        if (!context.backend->recv_burst(burst, context.config.max_burst)) {
            run_status = "error";
            run_error = "backend recv_burst failed";
            context.backend->release_burst(burst);
            break;
        }

        std::uint64_t burst_bytes = 0;
        std::size_t accepted_packets = 0U;
        for (const PacketDesc& packet : burst.packets) {
            const auto udp_frames = payload_assembler.push(packet);
            for (const auto& udp_frame : udp_frames) {
                const SamplePacketView parsed = parser.parse(udp_frame);
                if (!matches_packet_filter(context.config, parsed)) {
                    ++filtered_packets;
                    continue;
                }

                burst_bytes += udp_frame.udp_payload.size();
                ++accepted_packets;
                const SamplePacketValidation validation = validator.validate(parsed);
                if (!validation.ok) {
                    context.metrics->on_drop();
                    context.metrics->on_invalid_header(udp_frame.port_id);
                    if (invalid_dumped < 5U) {
                        PacketDesc diagnostic_packet;
                        diagnostic_packet.data = const_cast<std::uint8_t*>(udp_frame.udp_payload.data());
                        diagnostic_packet.len = static_cast<std::uint32_t>(udp_frame.udp_payload.size());
                        diagnostic_packet.queue_id = udp_frame.queue_id;
                        std::ostream& diagnostic_stream = status_output_ != nullptr ? *status_output_ : std::cerr;
                        emit_invalid_packet_diagnostic(diagnostic_stream, diagnostic_packet, parsed, validation);
                        ++invalid_dumped;
                    }
                    continue;
                }

                const ProtocolPacketView protocol_packet = sequence_interpreter.interpret(parsed);
                if (!protocol_packet.valid) {
                    context.metrics->on_drop();
                    context.metrics->on_invalid_header(udp_frame.port_id);
                    if (invalid_dumped < 5U) {
                        PacketDesc diagnostic_packet;
                        diagnostic_packet.data = const_cast<std::uint8_t*>(udp_frame.udp_payload.data());
                        diagnostic_packet.len = static_cast<std::uint32_t>(udp_frame.udp_payload.size());
                        diagnostic_packet.queue_id = udp_frame.queue_id;
                        SamplePacketValidation protocol_validation;
                        protocol_validation.ok = false;
                        protocol_validation.reason = protocol_packet.error_reason;
                        std::ostream& diagnostic_stream = status_output_ != nullptr ? *status_output_ : std::cerr;
                        emit_invalid_packet_diagnostic(diagnostic_stream, diagnostic_packet, parsed, protocol_validation);
                        ++invalid_dumped;
                    }
                    continue;
                }

                context.metrics->on_parsed_packet();
                context.metrics->on_port_packet(udp_frame.port_id, udp_frame.udp_payload.size());
                if (udp_frame.ts_ns != 0U) {
                    context.metrics->on_packet_latency_ns(udp_frame.ts_ns);
                }
                if (protocol_packet.kind == SamplePacketKind::control_table) {
                    context.metrics->on_control_table_packet();
                    cpi_stats[protocol_packet.cpi].control_table_packets += 1U;
                } else if (protocol_packet.kind == SamplePacketKind::data_packet) {
                    context.metrics->on_data_packet();
                    ProtocolChannelStats& per_channel = channel_stats[protocol_packet.channel];
                    per_channel.data_packets += 1U;
                    per_channel.iq_count += protocol_packet.iq_count;
                    per_channel.data_bytes += protocol_packet.iq_count * 4U;
                    per_channel.zero_padding_bytes += protocol_packet.zero_padding_bytes;
                    ProtocolCpiStats& stats = cpi_stats[protocol_packet.cpi];
                    stats.data_packets += 1U;
                    stats.prts.insert(protocol_packet.prt);
                    stats.channels.insert(protocol_packet.channel);
                    prt_coverage[{protocol_packet.cpi, protocol_packet.prt}][protocol_packet.channel].insert(protocol_packet.packet_index);
                    if (parsed.tail == 0x55AAFF30U) {
                        ++final_tail_packets;
                    }
                }

                CapturedPacket captured;
                captured.payload = udp_frame.udp_payload;
                captured.ts_ns = udp_frame.ts_ns;
                captured.port_id = udp_frame.port_id;
                captured.queue_id = udp_frame.queue_id;
                captured.face_id = udp_frame.face_id;
                captured.cpi = protocol_packet.cpi;
                captured.channel = protocol_packet.channel;
                captured.prt = protocol_packet.prt;
                captured.packet_index = protocol_packet.packet_index;
                captured.packet_kind = sample_packet_kind_name(protocol_packet.kind);
                captured.validation = "ok";
                unique_cpis.insert(protocol_packet.cpi);
                if (protocol_packet.kind == SamplePacketKind::data_packet) {
                    unique_prts.insert(protocol_packet.prt);
                    unique_channels.insert(protocol_packet.channel);
                }

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
        }

        if (accepted_packets > 0U) {
            context.metrics->on_burst(accepted_packets, burst_bytes);
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
            status_summary.filtered_packets = filtered_packets;
            status_summary.packet_count = artifacts.recorded_packets;
            status_summary.cpi_count = static_cast<std::uint64_t>(unique_cpis.size());
            status_summary.prt_count = static_cast<std::uint64_t>(unique_prts.size());
            status_summary.channel_count = static_cast<std::uint64_t>(unique_channels.size());
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
    summary.filtered_packets = filtered_packets;
    summary.packet_count = artifacts.recorded_packets;
    summary.cpi_count = static_cast<std::uint64_t>(unique_cpis.size());
    summary.prt_count = static_cast<std::uint64_t>(unique_prts.size());
    summary.channel_count = static_cast<std::uint64_t>(unique_channels.size());
    summary.final_tail_packets = final_tail_packets;
    for (const auto& entry : prt_coverage) {
        bool complete = true;
        for (std::uint16_t channel = 0; channel < 3U; ++channel) {
            const auto channel_it = entry.second.find(channel);
            if (channel_it == entry.second.end()) {
                complete = false;
                break;
            }
            for (std::uint16_t packet_index = 1U; packet_index <= 9U; ++packet_index) {
                if (channel_it->second.count(packet_index) == 0U) {
                    complete = false;
                    break;
                }
            }
            if (!complete) {
                break;
            }
        }
        if (complete) {
            ++summary.complete_prt_count;
        }
    }
    for (const auto& entry : channel_stats) {
        ProtocolChannelSummary channel_summary;
        channel_summary.channel = entry.first;
        channel_summary.channel_name = protocol_channel_name(entry.first);
        channel_summary.data_packets = entry.second.data_packets;
        channel_summary.iq_count = entry.second.iq_count;
        channel_summary.data_bytes = entry.second.data_bytes;
        channel_summary.zero_padding_bytes = entry.second.zero_padding_bytes;
        summary.protocol_channels.push_back(channel_summary);
    }
    for (const auto& entry : cpi_stats) {
        ProtocolCpiSummary cpi_summary;
        cpi_summary.cpi = entry.first;
        cpi_summary.control_table_packets = entry.second.control_table_packets;
        cpi_summary.data_packets = entry.second.data_packets;
        cpi_summary.prt_count = static_cast<std::uint64_t>(entry.second.prts.size());
        cpi_summary.channel_count = static_cast<std::uint64_t>(entry.second.channels.size());
        summary.protocol_cpis.push_back(cpi_summary);
    }
    merge_backend_stats(summary, context.backend->stats());
    summary.human_summary = build_human_summary(summary);

    if (context.config.run_until_stopped && status_output_ != nullptr) {
        print_status_snapshot(*status_output_, summary, end_time - start_time);
    }
    send_feedback_snapshot(context.config, summary, status_output_);

    return summary;
}

}  // namespace rxtech
