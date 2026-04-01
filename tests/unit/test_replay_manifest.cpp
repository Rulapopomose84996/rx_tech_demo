#include <cassert>

#include "rxtech/replay_manifest.h"

int main() {
    const rxtech::ReplayManifest manifest =
        rxtech::load_replay_manifest("data/cpi_0002_complete/cpi_0002_replay_manifest.json");

    assert(manifest.format_version == 1U);
    assert(manifest.sample_type == "complete_cpi_unit");
    assert(manifest.cpi == 2U);
    assert(manifest.total_udp_units == 1351U);
    assert(manifest.entries.size() == 1351U);

    const rxtech::ReplayEntry& first = manifest.entries.front();
    assert(first.sequence == 0U);
    assert(first.kind == "control_table");
    assert(first.file == "cpi_0002_control_table.bin");
    assert(first.offset_bytes == 0U);
    assert(first.length_bytes == 2048U);
    assert(first.cpi == 2U);

    const rxtech::ReplayEntry& second = manifest.entries[1];
    assert(second.sequence == 1U);
    assert(second.kind == "data_packet");
    assert(second.file == "cpi_0002_data_payloads.bin");
    assert(second.prt == 1U);
    assert(second.channel == 0U);
    assert(second.packet_index == 1U);
    assert(second.channel_name == "和路");

    return 0;
}
