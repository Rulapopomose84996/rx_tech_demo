#include "rxtech/scenario.h"

namespace rxtech {

Scenario load_scenario(const std::string& path) {
    Scenario scenario;
    scenario.scenario_name = path.empty() ? "default_scenario" : path;
    scenario.steps.push_back({"warmup", "steady", 1.0, 1, 1});
    scenario.steps.push_back({"measure", "steady", 4.8, 5, 1});
    return scenario;
}

}  // namespace rxtech
