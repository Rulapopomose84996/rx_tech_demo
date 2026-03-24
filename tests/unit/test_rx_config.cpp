#include <fstream>
#include <iostream>

#include "rxtech/rx_config.h"

int main() {
    const rxtech::RxConfig default_config = rxtech::load_default_config();
    if (default_config.interface_name != "receiver3") {
        std::cerr << "expected default interface receiver3, got "
                  << default_config.interface_name << '\n';
        return 1;
    }

    const char* path = "results/test_rx_config_generated.conf";
    {
        std::ofstream out(path, std::ios::trunc);
        out << "backend: socket\n";
        out << "mode: parse\n";
        out << "scenario: scenarios/three_face_burst.yaml\n";
        out << "output_dir: results/config_case\n";
        out << "interface_name: receiver3\n";
        out << "queue_id: 2\n";
        out << "duration_seconds: 12\n";
        out << "max_burst: 32\n";
        out << "cpu_cores: [16,17,18]\n";
        out << "enable_internal_traffic: true\n";
        out << "packet_size_bytes: 1024\n";
        out << "udp_port: 10001\n";
    }

    const rxtech::RxConfig config = rxtech::load_config_file(path);
    if (config.backend_name != "socket" || config.mode_name != "parse" ||
        config.scenario_path != "scenarios/three_face_burst.yaml" ||
        config.output_dir != "results/config_case" ||
        config.interface_name != "receiver3" || config.queue_id != 2U ||
        config.duration_seconds != 12U || config.max_burst != 32U ||
        !config.enable_internal_traffic || config.packet_size_bytes != 1024U ||
        config.udp_port != 10001U || config.cpu_cores.size() != 3U ||
        config.cpu_cores[0] != 16) {
        std::cerr << "config parsing regression in test_rx_config\n";
        return 1;
    }
    return 0;
}
