#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech {

struct ScenarioStep {
    std::string name;
    std::string traffic_profile;
    double target_rate_gbps = 0.0;
    std::uint32_t duration_seconds = 0;
    std::uint32_t face_count = 1;
};

struct Scenario {
    std::string scenario_name;
    std::vector<ScenarioStep> steps;
};

Scenario load_scenario(const std::string& path);

}  // namespace rxtech
