#include <fstream>
#include <iostream>

#include "rxtech/rx_config.h"

int main() {
    const rxtech::RxConfig default_config = rxtech::load_default_config();
    if (default_config.interface_name != "receiver0") {
        std::cerr << "expected default interface receiver0, got "
                  << default_config.interface_name << '\n';
        return 1;
    }

    const char* path = "test_rx_config_generated.conf";
    {
        std::ofstream out(path, std::ios::trunc);
        out << "backend: af_xdp\n";
        out << "output_dir: results/config_case\n";
        out << "interface_name: receiver1\n";
        out << "queue_id: 22\n";
        out << "duration_seconds: 12\n";
        out << "max_burst: 32\n";
        out << "cpu_cores: [16,17,18]\n";
        out << "packet_size_bytes: 1024\n";
        out << "run_until_stopped: true\n";
        out << "status_interval_seconds: 10\n";
        out << "feedback_interval_seconds: 1\n";
        out << "feedback_enabled: true\n";
        out << "feedback_host: 172.20.11.11\n";
        out << "feedback_bind_host: 172.20.11.100\n";
        out << "feedback_port: 9999\n";
        out << "reassembly_timeout_ms: 1500\n";
        out << "xdp_bind_mode: copy\n";
        out << "xdp_rx_ring_size: 512\n";
        out << "xdp_tx_ring_size: 128\n";
        out << "xdp_fill_ring_size: 1024\n";
        out << "xdp_completion_ring_size: 512\n";
        out << "xdp_frame_size: 4096\n";
        out << "xdp_frame_count: 8192\n";
        out << "xdp_poll_timeout_ms: 0\n";
    }

    const rxtech::RxConfig config = rxtech::load_config_file(path);
    if (config.backend_name != "af_xdp" || config.output_dir != "results/config_case" ||
        config.interface_name != "receiver1" || config.queue_id != 22U ||
        config.duration_seconds != 12U || config.max_burst != 32U ||
        config.packet_size_bytes != 1024U || config.cpu_cores.size() != 3U ||
        config.cpu_cores[0] != 16 || !config.run_until_stopped ||
        config.status_interval_seconds != 10U || config.feedback_interval_seconds != 1U || !config.feedback_enabled ||
        config.feedback_host != "172.20.11.11" || config.feedback_bind_host != "172.20.11.100" || config.feedback_port != 9999U ||
        config.reassembly_timeout_ms != 1500U || config.xdp_bind_mode != "copy" ||
        config.xdp_rx_ring_size != 512U || config.xdp_tx_ring_size != 128U ||
        config.xdp_fill_ring_size != 1024U || config.xdp_completion_ring_size != 512U ||
        config.xdp_frame_size != 4096U || config.xdp_frame_count != 8192U ||
        config.xdp_poll_timeout_ms != 0U) {
        std::cerr << "config parsing regression in test_rx_config\n";
        return 1;
    }
    return 0;
}
