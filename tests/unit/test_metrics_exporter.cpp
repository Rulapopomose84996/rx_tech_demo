#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#if defined(__unix__) || defined(__linux__)
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "metrics_exporter.h"
#include "rxtech/rx_config.h"

namespace
{

    rxtech::RunSummary make_summary()
    {
        rxtech::RunSummary summary;
        summary.run.backend_name = "socket";
        summary.run.status = "success";
        summary.run.structured_log_backend = "builtin";
        summary.backend.raw_rx_packets = 42U;
        summary.backend.raw_rx_bytes = 2048U;
        summary.protocol.rx_packets = 10U;
        summary.protocol.parsed_packets = 8U;
        summary.protocol.data_packets = 7U;
        summary.protocol.control_table_packets = 1U;
        summary.protocol.dropped_packets = 2U;
        summary.performance.actual_rx_gbps = 1.23;
        summary.performance.cpu_metrics_available = true;
        summary.performance.cpu_metrics_status = "ok";
        summary.performance.cpu_user_pct = 12.5;
        summary.performance.cpu_sys_pct = 1.5;
        summary.global_packet_sequence.available = true;
        summary.global_packet_sequence.checked_packets = 10U;
        summary.global_packet_sequence.gap_count = 1U;
        summary.global_packet_sequence.missing_packets = 2U;
        summary.global_packet_sequence.first_gap = "seq gap";
        return summary;
    }

} // namespace

int main()
{
    const rxtech::RunSummary summary = make_summary();

    const std::string prometheus = rxtech::MetricsExporter::render_prometheus_text(summary);
    assert(prometheus.find("rxtech_protocol_rx_packets 10") != std::string::npos);
    assert(prometheus.find("rxtech_sequence_gap_count 1") != std::string::npos);

    const std::string json_payload = rxtech::MetricsExporter::render_json_payload(summary);
    const nlohmann::json parsed_json = nlohmann::json::parse(json_payload);
    assert(parsed_json.at("run").at("backend") == "socket");
    assert(parsed_json.at("global_packet_sequence").at("missing_packets") == 2U);

    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.metrics_export_mode = "prometheus_text";
        config.operations.metrics_export_path = "test_metrics_export.prom";

        rxtech::MetricsExporter exporter(config, std::chrono::steady_clock::now());
        exporter.export_final(summary);

        std::ifstream input(config.operations.metrics_export_path);
        assert(input.is_open());
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        assert(content.find("rxtech_backend_raw_rx_packets 42") != std::string::npos);
        std::remove(config.operations.metrics_export_path.c_str());
    }

#if defined(__unix__) || defined(__linux__)
    {
        std::ostringstream socket_path_builder;
        socket_path_builder << "/tmp/test_metrics_export_" << ::getpid() << ".sock";
        const std::string socket_path = socket_path_builder.str();
        std::remove(socket_path.c_str());

        int server_fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        assert(server_fd >= 0);

        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        (void)::setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path.c_str());
        assert(::bind(server_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0);

        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.metrics_export_mode = "json_socket";
        config.operations.metrics_export_path = socket_path;

        rxtech::MetricsExporter exporter(config, std::chrono::steady_clock::now());
        exporter.export_final(summary);

        char buffer[8192] = {};
        const ssize_t received = ::recv(server_fd, buffer, sizeof(buffer), 0);
        assert(received > 0);
        const nlohmann::json socket_payload = nlohmann::json::parse(std::string(buffer, buffer + received));
        assert(socket_payload.at("protocol").at("parsed_packets") == 8U);

        ::close(server_fd);
        std::remove(socket_path.c_str());
    }
#endif

    return 0;
}
