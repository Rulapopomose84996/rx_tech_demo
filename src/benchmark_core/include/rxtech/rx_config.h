#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech {

struct RxConfig {
    std::string backend_name = "socket";
    std::string mode_name = "rx_only";
    std::string scenario_path;
    std::string output_dir = "results";
    std::string interface_name = "enP1s25f3";
    std::uint32_t queue_id = 0;
    std::uint32_t max_burst = 64;
    std::uint32_t duration_seconds = 5;
    std::vector<int> cpu_cores;
};

RxConfig load_default_config();

}  // namespace rxtech
