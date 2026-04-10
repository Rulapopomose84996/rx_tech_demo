#include "internal/metrics_exporter.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#if defined(__unix__) || defined(__linux__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "internal/structured_logger.h"

namespace rxtech
{

    namespace
    {

        nlohmann::json summary_to_json(const RunSummary &summary)
        {
            return {
                {"run",
                 {{"backend", summary.run.backend_name},
                  {"mode", summary.run.mode},
                  {"scenario", summary.run.scenario_name},
                  {"status", summary.run.status},
                  {"structured_log_backend", summary.run.structured_log_backend}}},
                {"backend",
                 {{"raw_rx_packets", summary.backend.raw_rx_packets},
                  {"raw_rx_bytes", summary.backend.raw_rx_bytes},
                  {"filtered_packets", summary.backend.filtered_packets},
                  {"dropped_packets", summary.backend.dropped_packets},
                  {"errors", summary.backend.errors},
                  {"kernel_drops", summary.backend.kernel_drops},
                  {"receive_batches", summary.backend.receive_batches},
                  {"max_burst_size", summary.backend.max_burst_size}}},
                {"protocol",
                 {{"rx_packets", summary.protocol.rx_packets},
                  {"rx_bytes", summary.protocol.rx_bytes},
                  {"parsed_packets", summary.protocol.parsed_packets},
                  {"control_table_packets", summary.protocol.control_table_packets},
                  {"data_packets", summary.protocol.data_packets},
                  {"dropped_packets", summary.protocol.dropped_packets},
                  {"cpi_count", summary.protocol.cpi_count},
                  {"prt_count", summary.protocol.prt_count},
                  {"channel_count", summary.protocol.channel_count},
                  {"complete_prt_count", summary.protocol.complete_prt_count}}},
                {"performance",
                 {{"actual_rx_gbps", summary.performance.actual_rx_gbps},
                  {"actual_rx_mpps", summary.performance.actual_rx_mpps},
                  {"cpu_user_pct", summary.performance.cpu_user_pct},
                  {"cpu_sys_pct", summary.performance.cpu_sys_pct},
                  {"cpu_peak_pct", summary.performance.cpu_peak_pct},
                  {"cpu_metrics_available", summary.performance.cpu_metrics_available},
                  {"cpu_metrics_status", summary.performance.cpu_metrics_status},
                  {"batch_avg", summary.performance.batch_avg},
                  {"batch_p99", summary.performance.batch_p99},
                  {"latency_p50_us", summary.performance.latency_p50_us},
                  {"latency_p99_us", summary.performance.latency_p99_us},
                  {"empty_poll_ratio", summary.performance.empty_poll_ratio},
                  {"output_backpressure_count", summary.performance.output_backpressure_count}}},
                {"global_packet_sequence",
                 {{"available", summary.global_packet_sequence.available},
                  {"status", summary.global_packet_sequence.status},
                  {"checked_packets", summary.global_packet_sequence.checked_packets},
                  {"gap_count", summary.global_packet_sequence.gap_count},
                  {"missing_packets", summary.global_packet_sequence.missing_packets},
                  {"first_gap", summary.global_packet_sequence.first_gap}}},
                {"metrics_export",
                 {{"enabled", summary.metrics_export.enabled},
                  {"mode", summary.metrics_export.mode},
                  {"target_path", summary.metrics_export.target_path},
                  {"status", summary.metrics_export.status},
                  {"success_count", summary.metrics_export.success_count},
                  {"error_count", summary.metrics_export.error_count}}},
            };
        }

    } // namespace

    MetricsExporter::MetricsExporter(const RxConfig &config, const std::chrono::steady_clock::time_point &start_time)
    {
        configure(config, start_time);
    }

    void MetricsExporter::configure(const RxConfig &config, const std::chrono::steady_clock::time_point &start_time)
    {
        mode_ = config.operations.metrics_export_mode;
        target_path_ = config.operations.metrics_export_path;
        enabled_ = mode_ != "none" && !target_path_.empty();

        const std::uint32_t interval_seconds = config.operations.metrics_export_interval_seconds != 0U
                                                   ? config.operations.metrics_export_interval_seconds
                                                   : config.operations.status_interval_seconds;
        interval_ = std::chrono::seconds(interval_seconds);
        next_export_at_ =
            interval_seconds == 0U ? std::chrono::steady_clock::time_point::max() : start_time + interval_;
        success_count_ = 0U;
        error_count_ = 0U;
        status_ = enabled_ ? "ready" : "disabled";
    }

    void MetricsExporter::maybe_export(const RunSummary &summary, const std::chrono::steady_clock::time_point &now)
    {
        if (!enabled_ || interval_.count() == 0 || now < next_export_at_)
        {
            return;
        }

        (void)export_once(summary);
        next_export_at_ = now + interval_;
    }

    void MetricsExporter::export_final(const RunSummary &summary)
    {
        if (!enabled_)
        {
            return;
        }
        (void)export_once(summary);
    }

    void MetricsExporter::populate_summary(RunSummary &summary) const
    {
        summary.metrics_export.enabled = enabled_;
        summary.metrics_export.mode = mode_;
        summary.metrics_export.target_path = target_path_;
        summary.metrics_export.status = status_;
        summary.metrics_export.success_count = success_count_;
        summary.metrics_export.error_count = error_count_;
    }

    std::string MetricsExporter::render_prometheus_text(const RunSummary &summary)
    {
        std::ostringstream out;
        out << "# rx_tech_demo metrics export\n";
        out << "# metrics_export_status " << summary.metrics_export.status << "\n";
        out << "rxtech_backend_raw_rx_packets " << summary.backend.raw_rx_packets << '\n';
        out << "rxtech_backend_raw_rx_bytes " << summary.backend.raw_rx_bytes << '\n';
        out << "rxtech_backend_filtered_packets " << summary.backend.filtered_packets << '\n';
        out << "rxtech_backend_dropped_packets " << summary.backend.dropped_packets << '\n';
        out << "rxtech_backend_kernel_drops " << summary.backend.kernel_drops << '\n';
        out << "rxtech_protocol_rx_packets " << summary.protocol.rx_packets << '\n';
        out << "rxtech_protocol_rx_bytes " << summary.protocol.rx_bytes << '\n';
        out << "rxtech_protocol_parsed_packets " << summary.protocol.parsed_packets << '\n';
        out << "rxtech_protocol_control_table_packets " << summary.protocol.control_table_packets << '\n';
        out << "rxtech_protocol_data_packets " << summary.protocol.data_packets << '\n';
        out << "rxtech_protocol_dropped_packets " << summary.protocol.dropped_packets << '\n';
        out << "rxtech_protocol_cpi_count " << summary.protocol.cpi_count << '\n';
        out << "rxtech_protocol_prt_count " << summary.protocol.prt_count << '\n';
        out << "rxtech_protocol_complete_prt_count " << summary.protocol.complete_prt_count << '\n';
        out << "rxtech_perf_actual_rx_gbps " << summary.performance.actual_rx_gbps << '\n';
        out << "rxtech_perf_actual_rx_mpps " << summary.performance.actual_rx_mpps << '\n';
        out << "rxtech_perf_cpu_user_pct " << summary.performance.cpu_user_pct << '\n';
        out << "rxtech_perf_cpu_sys_pct " << summary.performance.cpu_sys_pct << '\n';
        out << "rxtech_perf_cpu_peak_pct " << summary.performance.cpu_peak_pct << '\n';
        out << "rxtech_perf_output_backpressure_count " << summary.performance.output_backpressure_count << '\n';
        out << "rxtech_sequence_available " << (summary.global_packet_sequence.available ? 1 : 0) << '\n';
        out << "rxtech_sequence_checked_packets " << summary.global_packet_sequence.checked_packets << '\n';
        out << "rxtech_sequence_gap_count " << summary.global_packet_sequence.gap_count << '\n';
        out << "rxtech_sequence_missing_packets " << summary.global_packet_sequence.missing_packets << '\n';
        out << "rxtech_export_success_count " << summary.metrics_export.success_count << '\n';
        out << "rxtech_export_error_count " << summary.metrics_export.error_count << '\n';
        return out.str();
    }

    std::string MetricsExporter::render_json_payload(const RunSummary &summary)
    {
        return summary_to_json(summary).dump();
    }

    bool MetricsExporter::export_once(const RunSummary &summary)
    {
        RunSummary export_summary = summary;
        populate_summary(export_summary);

        bool ok = false;
        if (mode_ == "prometheus_text")
        {
            ok = export_prometheus_text(export_summary);
        }
        else if (mode_ == "json_socket")
        {
            ok = export_json_socket(export_summary);
        }

        if (ok)
        {
            ++success_count_;
            status_ = "ok";
        }
        else
        {
            ++error_count_;
            status_ = "error";
        }

        return ok;
    }

    bool MetricsExporter::export_prometheus_text(const RunSummary &summary)
    {
        std::ofstream output(target_path_, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            structured_log(StructuredLogLevel::error, "metrics_export_failed",
                           {{"mode", mode_}, {"target_path", target_path_}, {"reason", "open_failed"}});
            return false;
        }

        output << render_prometheus_text(summary);
        output.flush();
        const bool ok = output.good();
        if (!ok)
        {
            structured_log(StructuredLogLevel::error, "metrics_export_failed",
                           {{"mode", mode_}, {"target_path", target_path_}, {"reason", "write_failed"}});
        }
        return ok;
    }

    bool MetricsExporter::export_json_socket(const RunSummary &summary)
    {
#if defined(__unix__) || defined(__linux__)
        const std::string payload = render_json_payload(summary);
        int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd < 0)
        {
            structured_log(StructuredLogLevel::error, "metrics_export_failed",
                           {{"mode", mode_}, {"target_path", target_path_}, {"reason", "socket_create_failed"}});
            return false;
        }

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (target_path_.size() >= sizeof(address.sun_path))
        {
            ::close(fd);
            structured_log(StructuredLogLevel::error, "metrics_export_failed",
                           {{"mode", mode_}, {"target_path", target_path_}, {"reason", "path_too_long"}});
            return false;
        }

        std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", target_path_.c_str());
        const socklen_t address_length = static_cast<socklen_t>(sizeof(address.sun_family) + target_path_.size() + 1U);
        const ssize_t sent = ::sendto(fd, payload.data(), payload.size(), 0,
                                      reinterpret_cast<const sockaddr *>(&address), address_length);
        ::close(fd);
        if (sent < 0)
        {
            structured_log(StructuredLogLevel::error, "metrics_export_failed",
                           {{"mode", mode_}, {"target_path", target_path_}, {"reason", "send_failed"}});
            return false;
        }

        return true;
#else
        (void)summary;
        structured_log(StructuredLogLevel::error, "metrics_export_failed",
                       {{"mode", mode_}, {"target_path", target_path_}, {"reason", "unsupported_platform"}});
        return false;
#endif
    }

} // namespace rxtech
