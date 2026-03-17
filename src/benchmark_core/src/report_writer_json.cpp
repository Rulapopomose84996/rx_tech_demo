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

void write_summary_json(const RunSummary& summary, const std::string& output_dir) {
    create_dir_if_needed(output_dir);
    std::ofstream out(output_dir + "/summary.json", std::ios::trunc);
    out << "{\n";
    out << "  \"backend\": \"" << summary.backend << "\",\n";
    out << "  \"mode\": \"" << summary.mode << "\",\n";
    out << "  \"scenario\": \"" << summary.scenario << "\",\n";
    out << "  \"xdp_attach_mode\": \"" << summary.xdp_attach_mode << "\",\n";
    out << "  \"rx_packets\": " << summary.rx_packets << ",\n";
    out << "  \"rx_bytes\": " << summary.rx_bytes << ",\n";
    out << "  \"rx_polls\": " << summary.rx_polls << ",\n";
    out << "  \"empty_polls\": " << summary.empty_polls << ",\n";
    out << "  \"queue_id\": " << summary.queue_id << ",\n";
    out << "  \"xdp_prog_id\": " << summary.xdp_prog_id << ",\n";
    out << "  \"xsk_bind_flags\": " << summary.xsk_bind_flags << ",\n";
    out << "  \"umem_size\": " << summary.umem_size << ",\n";
    out << "  \"frame_size\": " << summary.frame_size << ",\n";
    out << "  \"fill_ring_size\": " << summary.fill_ring_size << ",\n";
    out << "  \"completion_ring_size\": " << summary.completion_ring_size << ",\n";
    out << "  \"actual_rx_gbps\": " << summary.actual_rx_gbps << ",\n";
    out << "  \"actual_rx_mpps\": " << summary.actual_rx_mpps << ",\n";
    out << "  \"empty_poll_ratio\": " << summary.empty_poll_ratio << "\n";
    out << "}\n";
}

}  // namespace rxtech
