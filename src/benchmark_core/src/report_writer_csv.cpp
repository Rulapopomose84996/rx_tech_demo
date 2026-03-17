#include "rxtech/report_writer.h"

#include <cerrno>
#include <fstream>
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

}  // namespace

void write_summary_csv(const RunSummary& summary, const std::string& output_dir) {
    create_dir_if_needed(output_dir);
    std::ofstream out(output_dir + "/summary.csv", std::ios::trunc);
    out << "backend,mode,scenario,packet_size_profile,xdp_attach_mode,xsk_mode,queue_id,xdp_prog_id,xsk_bind_flags,umem_size,frame_size,fill_ring_size,completion_ring_size,rx_packets,rx_bytes,parsed_packets,dropped_packets,backend_errors,nic_drops,pool_exhaustion_count,ring_high_watermark,rx_polls,empty_polls,actual_rx_gbps,actual_rx_mpps,cpu_user_pct,cpu_sys_pct,cpu_peak_pct,latency_p50_us,latency_p99_us,empty_poll_ratio,batch_avg,batch_p99\n";
    out << summary.backend << ','
        << summary.mode << ','
        << summary.scenario << ','
        << summary.packet_size_profile << ','
        << summary.xdp_attach_mode << ','
        << summary.xsk_mode << ','
        << summary.queue_id << ','
        << summary.xdp_prog_id << ','
        << summary.xsk_bind_flags << ','
        << summary.umem_size << ','
        << summary.frame_size << ','
        << summary.fill_ring_size << ','
        << summary.completion_ring_size << ','
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
        << summary.batch_p99 << '\n';
}

}  // namespace rxtech
