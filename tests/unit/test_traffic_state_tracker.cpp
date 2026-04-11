#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>

#include "traffic_state_tracker.h"

int main()
{
    rxtech::TrafficStateTracker tracker(3000U);

    const auto initial = tracker.snapshot();
    assert(initial.state == rxtech::TrafficState::idle);
    assert(!initial.first_seen_wall.has_value());

    tracker.observe_valid_business_packet(1'000'000'000ULL, "2026-04-12 00:00:01");
    const auto active = tracker.snapshot();
    assert(active.state == rxtech::TrafficState::active);
    assert(active.first_seen_wall.has_value());
    assert(active.last_seen_wall.has_value());

    const auto interrupted = tracker.observe_timeout(5'001'000'000ULL, "2026-04-12 00:00:05");
    assert(interrupted.has_value());
    assert(interrupted->transition == rxtech::TrafficTransition::interrupted);
    assert(tracker.snapshot().state == rxtech::TrafficState::interrupted);

    const auto resumed = tracker.observe_valid_business_packet(6'000'000'000ULL, "2026-04-12 00:00:06");
    assert(resumed.has_value());
    assert(resumed->transition == rxtech::TrafficTransition::resumed);
    assert(tracker.snapshot().state == rxtech::TrafficState::active);
    assert(tracker.snapshot().last_interrupted_wall == "2026-04-12 00:00:05");
    assert(tracker.snapshot().last_resumed_wall == "2026-04-12 00:00:06");
    return 0;
}
