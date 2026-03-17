#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech {

struct ScenarioStep {
    std::string name;
    std::string phase = "measure";
    std::string traffic_profile;
    std::string packet_size_profile;
    double target_rate_gbps = 0.0;
    double burst_multiplier = 1.0;
    std::uint32_t duration_seconds = 0;
    std::uint32_t face_count = 1;
    std::uint32_t packet_size_bytes = 0;
    std::uint32_t burst_window_ms = 0;
};

struct Scenario {
    std::string scenario_name;
    std::string packet_size_profile = "fixed";
    std::uint32_t default_packet_size_bytes = 0;
    std::vector<ScenarioStep> steps;
};

bool is_measure_step(const ScenarioStep& step);
Scenario load_scenario(const std::string& path);

}  // namespace rxtech
