#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "manifest_loader.h"

int main()
{
    const std::vector<rxtech::replay::ReplayEntry> entries =
        rxtech::replay::load_replay_entries("data/cpi_0002_complete");

    assert(!entries.empty());
    assert(entries.front().kind == rxtech::replay::ReplayEntry::Kind::control_table);
    assert(entries.front().cpi == 2U);
    assert(entries.size() > 1000U);

    const rxtech::replay::ReplayEntry &last = entries.back();
    assert(last.kind == rxtech::replay::ReplayEntry::Kind::data_packet);
    assert(last.cpi == 2U);
    assert(last.length == 2048U);
    return 0;
}
