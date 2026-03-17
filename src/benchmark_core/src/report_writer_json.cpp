#include "rxtech/report_writer.h"

#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace rxtech {

namespace {

void create_dir_if_needed(const std::string& path) {
    if (path.empty()) {
        return;
    }

    std::string current;
    for (char ch : path) {
        current.push_back(ch);
        if (ch != '/' && ch != '\\') {
            continue;
        }

        if (current.size() <= 1U) {
            continue;
        }

#ifdef _WIN32
        if (_mkdir(current.c_str()) != 0 && errno != EEXIST) {
            return;
        }
#else
        if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
            return;
        }
#endif
    }

#ifdef _WIN32
    (void)_mkdir(path.c_str());
#else
    (void)mkdir(path.c_str(), 0755);
#endif
}

std::string escape_json(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

void write_step_json(std::ostream& out, const StepSummary& step) {
    out << "    {\n";
    out << "      \"step_index\": " << step.step_index << ",\n";
    out << "      \"step_name\": \"" << escape_json(step.step_name) << "\",\n";
    out << "      \"phase\": \"" << escape_json(step.phase) << "\",\n";
    out << "      \"traffic_profile\": \"" << escape_json(step.traffic_profile) << "\",\n";
    out << "      \"packet_size_profile\": \"" << escape_json(step.packet_size_profile) << "\",\n";
    out << "      \"run_status\": \"" << escape_json(step.run_status) << "\",\n";
    out << "      \"error_message\": \"" << escape_json(step.error_message) << "\",\n";
    out << "      \"target_rate_gbps\": " << step.target_rate_gbps << ",\n";
    out << "      \"burst_multiplier\": " << step.burst_multiplier << ",\n";
    out << "      \"duration_seconds\": " << step.duration_seconds << ",\n";
    out << "      \"face_count\": " << step.face_count << ",\n";
    out << "      \"packet_size_bytes\": " << step.packet_size_bytes << ",\n";
    out << "      \"burst_window_ms\": " << step.burst_window_ms << ",\n";
    out << "      \"rx_packets\": " << step.rx_packets << ",\n";
    out << "      \"rx_bytes\": " << step.rx_bytes << ",\n";
    out << "      \"parsed_packets\": " << step.parsed_packets << ",\n";
    out << "      \"dropped_packets\": " << step.dropped_packets << ",\n";
    out << "      \"backend_errors\": " << step.backend_errors << ",\n";
    out << "      \"nic_drops\": " << step.nic_drops << ",\n";
    out << "      \"pool_exhaustion_count\": " << step.pool_exhaustion_count << ",\n";
    out << "      \"ring_high_watermark\": " << step.ring_high_watermark << ",\n";
    out << "      \"rx_polls\": " << step.rx_polls << ",\n";
    out << "      \"empty_polls\": " << step.empty_polls << ",\n";
    out << "      \"actual_rx_gbps\": " << step.actual_rx_gbps << ",\n";
    out << "      \"actual_rx_mpps\": " << step.actual_rx_mpps << ",\n";
    out << "      \"cpu_user_pct\": " << step.cpu_user_pct << ",\n";
    out << "      \"cpu_sys_pct\": " << step.cpu_sys_pct << ",\n";
    out << "      \"cpu_peak_pct\": " << step.cpu_peak_pct << ",\n";
    out << "      \"latency_p50_us\": " << step.latency_p50_us << ",\n";
    out << "      \"latency_p99_us\": " << step.latency_p99_us << ",\n";
    out << "      \"batch_avg\": " << step.batch_avg << ",\n";
    out << "      \"empty_poll_ratio\": " << step.empty_poll_ratio << ",\n";
    out << "      \"batch_p99\": " << step.batch_p99 << ",\n";
    out << "      \"cpu_metrics_available\": " << (step.cpu_metrics_available ? "true" : "false") << ",\n";
    out << "      \"cpu_metrics_status\": \"" << escape_json(step.cpu_metrics_status) << "\"\n";
    out << "    }";
}

}  // namespace

void write_summary_json(const RunSummary& summary, const std::string& output_dir) {
    create_dir_if_needed(output_dir);
    std::ofstream out(output_dir + "/summary.json", std::ios::trunc);
    out << "{\n";
    out << "  \"backend\": \"" << escape_json(summary.backend) << "\",\n";
    out << "  \"mode\": \"" << escape_json(summary.mode) << "\",\n";
    out << "  \"scenario\": \"" << escape_json(summary.scenario) << "\",\n";
    out << "  \"packet_size_profile\": \"" << escape_json(summary.packet_size_profile) << "\",\n";
    out << "  \"xdp_attach_mode\": \"" << escape_json(summary.xdp_attach_mode) << "\",\n";
    out << "  \"xsk_mode\": \"" << escape_json(summary.xsk_mode) << "\",\n";
    out << "  \"run_status\": \"" << escape_json(summary.run_status) << "\",\n";
    out << "  \"error_message\": \"" << escape_json(summary.error_message) << "\",\n";
    out << "  \"backend_status\": \"" << escape_json(summary.backend_status) << "\",\n";
    out << "  \"backend_reason\": \"" << escape_json(summary.backend_reason) << "\",\n";
    out << "  \"rx_packets\": " << summary.rx_packets << ",\n";
    out << "  \"rx_bytes\": " << summary.rx_bytes << ",\n";
    out << "  \"parsed_packets\": " << summary.parsed_packets << ",\n";
    out << "  \"dropped_packets\": " << summary.dropped_packets << ",\n";
    out << "  \"backend_errors\": " << summary.backend_errors << ",\n";
    out << "  \"nic_drops\": " << summary.nic_drops << ",\n";
    out << "  \"pool_exhaustion_count\": " << summary.pool_exhaustion_count << ",\n";
    out << "  \"ring_high_watermark\": " << summary.ring_high_watermark << ",\n";
    out << "  \"rx_polls\": " << summary.rx_polls << ",\n";
    out << "  \"empty_polls\": " << summary.empty_polls << ",\n";
    out << "  \"queue_id\": " << summary.queue_id << ",\n";
    out << "  \"xdp_prog_id\": " << summary.xdp_prog_id << ",\n";
    out << "  \"xsk_bind_flags\": " << summary.xsk_bind_flags << ",\n";
    out << "  \"umem_size\": " << summary.umem_size << ",\n";
    out << "  \"frame_size\": " << summary.frame_size << ",\n";
    out << "  \"fill_ring_size\": " << summary.fill_ring_size << ",\n";
    out << "  \"completion_ring_size\": " << summary.completion_ring_size << ",\n";
    out << "  \"total_step_count\": " << summary.total_step_count << ",\n";
    out << "  \"measure_step_count\": " << summary.measure_step_count << ",\n";
    out << "  \"scenario_duration_seconds\": " << summary.scenario_duration_seconds << ",\n";
    out << "  \"face_count\": " << summary.face_count << ",\n";
    out << "  \"packet_size_bytes\": " << summary.packet_size_bytes << ",\n";
    out << "  \"burst_window_ms\": " << summary.burst_window_ms << ",\n";
    out << "  \"target_rate_gbps\": " << summary.target_rate_gbps << ",\n";
    out << "  \"burst_multiplier\": " << summary.burst_multiplier << ",\n";
    out << "  \"actual_rx_gbps\": " << summary.actual_rx_gbps << ",\n";
    out << "  \"actual_rx_mpps\": " << summary.actual_rx_mpps << ",\n";
    out << "  \"cpu_user_pct\": " << summary.cpu_user_pct << ",\n";
    out << "  \"cpu_sys_pct\": " << summary.cpu_sys_pct << ",\n";
    out << "  \"cpu_peak_pct\": " << summary.cpu_peak_pct << ",\n";
    out << "  \"latency_p50_us\": " << summary.latency_p50_us << ",\n";
    out << "  \"latency_p99_us\": " << summary.latency_p99_us << ",\n";
    out << "  \"batch_avg\": " << summary.batch_avg << ",\n";
    out << "  \"batch_p99\": " << summary.batch_p99 << ",\n";
    out << "  \"empty_poll_ratio\": " << summary.empty_poll_ratio << ",\n";
    out << "  \"backend_available\": " << (summary.backend_available ? "true" : "false") << ",\n";
    out << "  \"cpu_metrics_available\": " << (summary.cpu_metrics_available ? "true" : "false") << ",\n";
    out << "  \"cpu_metrics_status\": \"" << escape_json(summary.cpu_metrics_status) << "\"\n";
    out << "}\n";
}

void write_step_summaries_json(const std::vector<StepSummary>& steps, const std::string& output_dir) {
    create_dir_if_needed(output_dir);
    std::ofstream out(output_dir + "/steps.json", std::ios::trunc);
    out << "{\n";
    out << "  \"steps\": [\n";
    for (std::size_t index = 0; index < steps.size(); ++index) {
        write_step_json(out, steps[index]);
        if (index + 1U < steps.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
}

}  // namespace rxtech
