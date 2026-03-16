#include <cassert>

#include "rxtech/scenario.h"

int main() {
    const rxtech::Scenario scenario = rxtech::load_scenario("scenarios/single_face_steady.yaml");
    assert(!scenario.steps.empty());
    return 0;
}
