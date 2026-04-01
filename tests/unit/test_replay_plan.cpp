#include <cassert>

#include "rxtech/replay_sender.h"

int main() {
    const rxtech::ReplayManifest manifest =
        rxtech::load_replay_manifest("data/cpi_0002_complete/cpi_0002_replay_manifest.json");
    const rxtech::ReplayPlan plan =
        rxtech::build_replay_plan("data/cpi_0002_complete", manifest);

    assert(plan.datagrams.size() == 1351U);

    const rxtech::ReplayDatagram& first = plan.datagrams.front();
    assert(first.sequence == 0U);
    assert(first.kind == "control_table");
    assert(first.payload.size() == 2048U);

    const rxtech::ReplayDatagram& second = plan.datagrams[1];
    assert(second.sequence == 1U);
    assert(second.kind == "data_packet");
    assert(second.payload.size() == 2048U);
    assert(second.prt == 1U);
    assert(second.channel == 0U);
    assert(second.packet_index == 1U);

    return 0;
}
