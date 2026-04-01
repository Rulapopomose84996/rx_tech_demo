#include "rxtech/receive_runner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rxtech {

namespace {

std::atomic<bool> g_stop_requested{false};

struct CapturedPacket {
    std::vector<std::uint8_t> payload;
    std::uint64_t ts_ns = 0;
    std::uint32_t port_id = 0;
    std::uint32_t queue_id = 0;
    std::uint32_t face_id = 0;
};

class SignalHandlerGuard {
public:
    SignalHandlerGuard() {
        previous_sigint_ = std::signal(SIGINT, signal_handler);
        previous_sigterm_ = std::signal(SIGTERM, signal_handler);
    }

    ~SignalHandlerGuard() {
        std::signal(SIGINT, previous_sigint_);
        std::signal(SIGTERM, previous_sigterm_);
    }

private:
    using SignalHandler = void (*)(int);

    static void signal_handler(int) {
        g_stop_requested.store(true);
    }

    SignalHandler previous_sigint_ = SIG_DFL;
    SignalHandler previous_sigterm_ = SIG_DFL;
};

bool is_path_separator(char ch) {
    return ch == '/' || ch == '\\';
}

void create_directory_if_needed(const std::string& path) {
    if (path.empty()) {
        return;
    }

#ifdef _WIN32
    const int result = _mkdir(path.c_str());
    if (result != 0 && errno != EEXIST) {
        throw std::runtime_error("failed to create directory: " + path);
    }
#else
    const int result = mkdir(path.c_str(), 0755);
    if (result != 0 && errno != EEXIST) {
        throw std::runtime_error("failed to create directory: " + path);
    }
#endif
}

void ensure_parent_directory(const std::string& file_path) {
    std::string normalized = file_path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const std::size_t separator_pos = normalized.find_last_of('/');
    if (separator_pos == std::string::npos) {
        return;
    }

    const std::string parent = normalized.substr(0U, separator_pos);
    if (parent.empty()) {
        return;
    }

    std::size_t start = 0;
    if (parent.size() >= 2U && parent[1] == ':') {
        start = 2U;
    } else if (is_path_separator(parent[0])) {
        start = 1U;
    }

    std::string current = parent.substr(0U, start);
    while (start < parent.size()) {
        while (start < parent.size() && is_path_separator(parent[start])) {
            if (current.empty()) {
                current.push_back('/');
            }
            ++start;
        }
        const std::size_t next = parent.find('/', start);
        const std::string part = parent.substr(start, next == std::string::npos ? std::string::npos : next - start);
        if (!part.empty()) {
            if (!current.empty() && !is_path_separator(current.back()) && current.back() != ':') {
                current.push_back('/');
            }
            current += part;
            create_directory_if_needed(current);
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1U;
    }
}

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
        << " captured_packets=" << summary.captured_packets
        << " recorded_packets=" << summary.recorded_packets
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
            << ",\"captured_packets\":" << summary.captured_packets
            << ",\"recorded_packets\":" << summary.recorded_packets
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

void write_capture_index_header(std::ofstream& index_stream) {
    index_stream << "sequence,offset,length,ts_ns,port_id,queue_id,face_id\n";
}

RunSummary make_unavailable_summary(const ReceiveContext& context,
                                    const BackendInitResult& init_result) {
    RunSummary summary;
    summary.backend = context.backend != nullptr ? context.backend->name() : context.config.backend_name;
    summary.run_status = init_result.available ? "error" : "unavailable";
    summary.error_message = init_result.reason;
    summary.backend_available = init_result.available;
    summary.backend_status = init_result.available ? "available" : "unavailable";
    summary.backend_reason = init_result.reason;
    return summary;
}

}  // namespace

void request_receive_stop() {
    g_stop_requested.store(true);
}

void reset_receive_stop() {
    g_stop_requested.store(false);
}

void ReceiveRunner::set_status_output(std::ostream* output) {
    status_output_ = output;
}

RunSummary ReceiveRunner::run(ReceiveContext& context) {
    if (!context.backend || !context.metrics) {
        throw std::runtime_error("receive context is incomplete");
    }

    reset_receive_stop();
    SignalHandlerGuard signal_guard;

    const BackendInitResult init_result = context.backend->init(context.config);
    if (!init_result.ok) {
        RunSummary summary = make_unavailable_summary(context, init_result);
        context.backend->shutdown();
        return summary;
    }

    const std::string output_dir = context.config.output_dir.empty() ? "results" : context.config.output_dir;
    const std::string capture_packets_path = output_dir + "/capture_packets.bin";
    const std::string capture_index_path = output_dir + "/capture_index.csv";
    ensure_parent_directory(capture_packets_path);
    ensure_parent_directory(capture_index_path);

    std::ofstream capture_packets_stream(capture_packets_path, std::ios::binary | std::ios::trunc);
    std::ofstream capture_index_stream(capture_index_path, std::ios::trunc);
    if (!capture_packets_stream.is_open()) {
        throw std::runtime_error("failed to open capture file: " + capture_packets_path);
    }
    if (!capture_index_stream.is_open()) {
        throw std::runtime_error("failed to open capture index file: " + capture_index_path);
    }
    write_capture_index_header(capture_index_stream);

    std::vector<CapturedPacket> captured_packets;
    std::uint64_t recorded_bytes = 0;
    std::uint64_t recorded_packets = 0;
    std::uint64_t file_offset = 0;

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.duration_seconds));
    const auto status_interval = std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.status_interval_seconds));
    const auto feedback_interval = std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.feedback_interval_seconds));
    auto next_status_at = start_time + status_interval;
    auto next_feedback_at = start_time + feedback_interval;
    std::string run_status = "success";
    std::string run_error;

    while (context.config.run_until_stopped ? !g_stop_requested.load() : (std::chrono::steady_clock::now() < deadline)) {
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
            if (packet.ts_ns != 0U) {
                context.metrics->on_packet_latency_ns(packet.ts_ns <= 0U ? 0U : (packet.ts_ns));
            }
            context.metrics->on_port_packet(packet.port_id, packet.len);

            CapturedPacket captured;
            captured.payload.assign(packet.data, packet.data + packet.len);
            captured.ts_ns = packet.ts_ns;
            captured.port_id = packet.port_id;
            captured.queue_id = packet.queue_id;
            captured.face_id = packet.face_id;
            captured_packets.push_back(captured);

            capture_packets_stream.write(reinterpret_cast<const char*>(captured.payload.data()),
                                         static_cast<std::streamsize>(captured.payload.size()));
            capture_index_stream << recorded_packets
                                 << ',' << file_offset
                                 << ',' << captured.payload.size()
                                 << ',' << captured.ts_ns
                                 << ',' << captured.port_id
                                 << ',' << captured.queue_id
                                 << ',' << captured.face_id
                                 << '\n';
            file_offset += captured.payload.size();
            recorded_bytes += captured.payload.size();
            ++recorded_packets;
        }
        if (!burst.packets.empty()) {
            context.metrics->on_burst(burst.packets.size(), burst_bytes);
        }
        context.backend->release_burst(burst);

        const auto now = std::chrono::steady_clock::now();
        if (context.config.run_until_stopped && ((status_output_ != nullptr && now >= next_status_at) ||
                                                 (context.config.feedback_enabled && now >= next_feedback_at))) {
            RunSummary status_summary =
                context.metrics->finalize(context.backend->name(), "capture", "direct_receive", std::max<std::uint32_t>(
                    1U, static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count())));
            status_summary.captured_packets = static_cast<std::uint64_t>(captured_packets.size());
            status_summary.captured_bytes = recorded_bytes;
            status_summary.recorded_packets = recorded_packets;
            status_summary.recorded_bytes = recorded_bytes;
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

    capture_packets_stream.flush();
    capture_index_stream.flush();

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed_seconds = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()));

    RunSummary summary = context.metrics->finalize(context.backend->name(), "capture", "direct_receive", elapsed_seconds);
    summary.run_status = run_status;
    summary.error_message = run_error;
    summary.backend_available = true;
    summary.backend_status = "available";
    summary.capture_packets_path = capture_packets_path;
    summary.capture_index_path = capture_index_path;
    summary.captured_packets = static_cast<std::uint64_t>(captured_packets.size());
    summary.captured_bytes = recorded_bytes;
    summary.recorded_packets = recorded_packets;
    summary.recorded_bytes = recorded_bytes;
    merge_backend_stats(summary, context.backend->stats());

    if (context.config.run_until_stopped && status_output_ != nullptr) {
        print_status_snapshot(*status_output_, summary, end_time - start_time);
    }
    send_feedback_snapshot(context.config, summary, status_output_);

    context.backend->shutdown();
    return summary;
}

}  // namespace rxtech
