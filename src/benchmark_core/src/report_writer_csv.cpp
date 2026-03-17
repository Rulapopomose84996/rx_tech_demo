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

std::string csv_quote(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

}  // namespace

void write_summary_csv(const RunSummary& summary, const std::string& output_dir) {
    create_dir_if_needed(output_dir);
    std::ofstream out(output_dir + "/summary.csv", std::ios::trunc);
    out << "backend,mode,scenario,packet_size_profile,xdp_attach_mode,xsk_mode,run_status,error_message,backend_status,backend_reason,queue_id,xdp_prog_id,xsk_bind_flags,umem_size,frame_size,fill_ring_size,completion_ring_size,total_step_count,measure_step_count,scenario_duration_seconds,face_count,packet_size_bytes,burst_window_ms,target_rate_gbps,burst_multiplier,rx_packets,rx_bytes,parsed_packets,dropped_packets,backend_errors,nic_drops,pool_exhaustion_count,ring_high_watermark,rx_polls,empty_polls,actual_rx_gbps,actual_rx_mpps,cpu_user_pct,cpu_sys_pct,cpu_peak_pct,latency_p50_us,latency_p99_us,empty_poll_ratio,batch_avg,batch_p99,backend_available,cpu_metrics_available,cpu_metrics_status\n";
    out << csv_quote(summary.backend) << ','
        << csv_quote(summary.mode) << ','
        << csv_quote(summary.scenario) << ','
        << csv_quote(summary.packet_size_profile) << ','
        << csv_quote(summary.xdp_attach_mode) << ','
        << csv_quote(summary.xsk_mode) << ','
        << csv_quote(summary.run_status) << ','
        << csv_quote(summary.error_message) << ','
        << csv_quote(summary.backend_status) << ','
        << csv_quote(summary.backend_reason) << ','
        << summary.queue_id << ','
        << summary.xdp_prog_id << ','
        << summary.xsk_bind_flags << ','
        << summary.umem_size << ','
        << summary.frame_size << ','
        << summary.fill_ring_size << ','
        << summary.completion_ring_size << ','
        << summary.total_step_count << ','
        << summary.measure_step_count << ','
        << summary.scenario_duration_seconds << ','
        << summary.face_count << ','
        << summary.packet_size_bytes << ','
        << summary.burst_window_ms << ','
        << summary.target_rate_gbps << ','
        << summary.burst_multiplier << ','
        << summary.rx_packets << ','
        << summary.rx_bytes << ','
        << summary.parsed_packets << ','
        << summary.dropped_packets << ','
        << summary.backend_errors << ','
        << summary.nic_drops << ','
        << summary.pool_exhaustion_count << ','
        << summary.ring_high_watermark << ','
        << summary.rx_polls << ','
        << summary.empty_polls << ','
        << summary.actual_rx_gbps << ','
        << summary.actual_rx_mpps << ','
        << summary.cpu_user_pct << ','
        << summary.cpu_sys_pct << ','
        << summary.cpu_peak_pct << ','
        << summary.latency_p50_us << ','
        << summary.latency_p99_us << ','
        << summary.empty_poll_ratio << ','
        << summary.batch_avg << ','
        << summary.batch_p99 << ','
        << (summary.backend_available ? "true" : "false") << ','
        << (summary.cpu_metrics_available ? "true" : "false") << ','
        << csv_quote(summary.cpu_metrics_status) << '\n';
}

void write_step_summaries_csv(const std::vector<StepSummary>& steps, const std::string& output_dir) {
    create_dir_if_needed(output_dir);
    std::ofstream out(output_dir + "/steps.csv", std::ios::trunc);
    out << "step_index,step_name,phase,traffic_profile,packet_size_profile,run_status,error_message,target_rate_gbps,burst_multiplier,duration_seconds,face_count,packet_size_bytes,burst_window_ms,rx_packets,rx_bytes,parsed_packets,dropped_packets,backend_errors,nic_drops,pool_exhaustion_count,ring_high_watermark,rx_polls,empty_polls,actual_rx_gbps,actual_rx_mpps,cpu_user_pct,cpu_sys_pct,cpu_peak_pct,latency_p50_us,latency_p99_us,batch_avg,empty_poll_ratio,batch_p99,cpu_metrics_available,cpu_metrics_status\n";
    for (const StepSummary& step : steps) {
        out << step.step_index << ','
            << csv_quote(step.step_name) << ','
            << csv_quote(step.phase) << ','
            << csv_quote(step.traffic_profile) << ','
            << csv_quote(step.packet_size_profile) << ','
            << csv_quote(step.run_status) << ','
            << csv_quote(step.error_message) << ','
            << step.target_rate_gbps << ','
            << step.burst_multiplier << ','
            << step.duration_seconds << ','
            << step.face_count << ','
            << step.packet_size_bytes << ','
            << step.burst_window_ms << ','
            << step.rx_packets << ','
            << step.rx_bytes << ','
            << step.parsed_packets << ','
            << step.dropped_packets << ','
            << step.backend_errors << ','
            << step.nic_drops << ','
            << step.pool_exhaustion_count << ','
            << step.ring_high_watermark << ','
            << step.rx_polls << ','
            << step.empty_polls << ','
            << step.actual_rx_gbps << ','
            << step.actual_rx_mpps << ','
            << step.cpu_user_pct << ','
            << step.cpu_sys_pct << ','
            << step.cpu_peak_pct << ','
            << step.latency_p50_us << ','
            << step.latency_p99_us << ','
            << step.batch_avg << ','
            << step.empty_poll_ratio << ','
            << step.batch_p99 << ','
            << (step.cpu_metrics_available ? "true" : "false") << ','
            << csv_quote(step.cpu_metrics_status) << '\n';
    }
}

}  // namespace rxtech
