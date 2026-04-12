#include "internal/traffic_state_tracker.h"

#include <chrono>
#include <ctime>

namespace rxtech
{

    namespace
    {
        std::string format_wall_clock_now()
        {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local_time{};
#ifdef _WIN32
            localtime_s(&local_time, &now);
#else
            localtime_r(&now, &local_time);
#endif
            char buffer[32] = {};
            if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time) == 0U)
            {
                return {};
            }
            return buffer;
        }
    } // namespace

    TrafficStateTracker::TrafficStateTracker(std::uint32_t interrupt_timeout_ms)
        : interrupt_timeout_ns_(static_cast<std::uint64_t>(interrupt_timeout_ms) * 1000ULL * 1000ULL)
    {
    }

    std::optional<TrafficTransitionEvent> TrafficStateTracker::observe_valid_business_packet(std::uint64_t monotonic_ns)
    {
        snapshot_.last_seen_monotonic_ns = monotonic_ns;

        if (!snapshot_.first_seen_wall.has_value())
        {
            const std::string wall_time = format_wall_clock_now();
            snapshot_.first_seen_wall = wall_time;
            snapshot_.last_seen_wall = wall_time;
            snapshot_.state = TrafficState::active;
            return TrafficTransitionEvent{TrafficTransition::first_seen, wall_time, monotonic_ns};
        }

        if (snapshot_.state == TrafficState::interrupted)
        {
            const std::string wall_time = format_wall_clock_now();
            snapshot_.state = TrafficState::active;
            snapshot_.last_seen_wall = wall_time;
            snapshot_.last_resumed_wall = wall_time;
            return TrafficTransitionEvent{TrafficTransition::resumed, wall_time, monotonic_ns};
        }

        snapshot_.state = TrafficState::active;
        return std::nullopt;
    }

    std::optional<TrafficTransitionEvent> TrafficStateTracker::observe_timeout(std::uint64_t monotonic_ns)
    {
        if (snapshot_.state != TrafficState::active)
        {
            return std::nullopt;
        }
        if (snapshot_.last_seen_monotonic_ns == 0U || monotonic_ns <= snapshot_.last_seen_monotonic_ns)
        {
            return std::nullopt;
        }
        if (interrupt_timeout_ns_ == 0U || monotonic_ns - snapshot_.last_seen_monotonic_ns < interrupt_timeout_ns_)
        {
            return std::nullopt;
        }

        const std::string wall_time = format_wall_clock_now();
        snapshot_.state = TrafficState::interrupted;
        snapshot_.last_interrupted_wall = wall_time;
        return TrafficTransitionEvent{TrafficTransition::interrupted, wall_time, monotonic_ns};
    }

    TrafficStateSnapshot TrafficStateTracker::snapshot() const noexcept
    {
        return snapshot_;
    }

} // namespace rxtech
