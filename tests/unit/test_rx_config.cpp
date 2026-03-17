#include <cassert>
#include <fstream>

#include "rxtech/rx_config.h"

int main() {
    const char* path = "test_rx_config.conf";
    {
        std::ofstream out(path, std::ios::trunc);
        out << "backend: socket\n";
        out << "mode: parse\n";
        out << "scenario: scenarios/three_face_burst.yaml\n";
        out << "output_dir: results/config_case\n";
        out << "interface_name: enP1s25f3\n";
        out << "queue_id: 2\n";
        out << "duration_seconds: 12\n";
        out << "max_burst: 32\n";
        out << "cpu_cores: [16,17,18]\n";
        out << "enable_internal_traffic: true\n";
        out << "packet_size_bytes: 1024\n";
        out << "udp_port: 10001\n";
    }

    const rxtech::RxConfig config = rxtech::load_config_file(path);
    assert(config.backend_name == "socket");
    assert(config.mode_name == "parse");
    assert(config.scenario_path == "scenarios/three_face_burst.yaml");
    assert(config.output_dir == "results/config_case");
    assert(config.queue_id == 2U);
    assert(config.duration_seconds == 12U);
    assert(config.max_burst == 32U);
    assert(config.enable_internal_traffic);
    assert(config.packet_size_bytes == 1024U);
    assert(config.udp_port == 10001U);
    assert(config.cpu_cores.size() == 3U);
    assert(config.cpu_cores[0] == 16);
    return 0;
}
