#include <cassert>

#include "rxtech/scenario.h"

int main() {
    const rxtech::Scenario scenario = rxtech::load_scenario("scenarios/single_face_steady.yaml");
    assert(!scenario.steps.empty());
    assert(scenario.scenario_name == "single_face_steady");
    assert(scenario.steps.front().traffic_profile == "steady");
    assert(scenario.steps.front().phase == "measure");
    assert(scenario.steps.front().packet_size_profile == "fixed_512");
    assert(scenario.steps.front().packet_size_bytes == 512U);
    assert(scenario.steps.front().face_count == 1U);

    const rxtech::Scenario smoke = rxtech::load_scenario("smoke");
    assert(smoke.steps.size() == 2U);
    assert(smoke.steps.front().phase == "warmup");
    assert(smoke.steps.back().phase == "measure");
    return 0;
}
