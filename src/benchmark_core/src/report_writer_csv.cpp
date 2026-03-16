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
    out << "backend,mode,scenario,rx_packets,rx_bytes,actual_rx_gbps,actual_rx_mpps,batch_avg,batch_p99\n";
    out << summary.backend << ','
        << summary.mode << ','
        << summary.scenario << ','
        << summary.rx_packets << ','
        << summary.rx_bytes << ','
        << summary.actual_rx_gbps << ','
        << summary.actual_rx_mpps << ','
        << summary.batch_avg << ','
        << summary.batch_p99 << '\n';
}

}  // namespace rxtech
