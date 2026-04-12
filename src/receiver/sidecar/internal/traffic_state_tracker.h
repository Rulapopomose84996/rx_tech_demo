#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rxtech
{

    enum class TrafficState
    {
        idle,
        active,
        interrupted,
    };

    enum class TrafficTransition
    {
        first_seen,
        interrupted,
        resumed,
    };

    struct TrafficTransitionEvent
    {
        TrafficTransition transition;
        std::string wall_time;
        std::uint64_t monotonic_ns = 0U;
    };

    struct TrafficStateSnapshot
    {
        TrafficState state = TrafficState::idle;
        std::optional<std::string> first_seen_wall;
        std::optional<std::string> last_seen_wall;
        std::optional<std::string> last_interrupted_wall;
        std::optional<std::string> last_resumed_wall;
        std::uint64_t last_seen_monotonic_ns = 0U;
    };

    class TrafficStateTracker
    {
      public:
        explicit TrafficStateTracker(std::uint32_t interrupt_timeout_ms);
        std::optional<TrafficTransitionEvent> observe_valid_business_packet(std::uint64_t monotonic_ns);
        std::optional<TrafficTransitionEvent> observe_timeout(std::uint64_t monotonic_ns);
        TrafficStateSnapshot snapshot() const noexcept;

      private:
        std::uint64_t interrupt_timeout_ns_ = 0U;
        TrafficStateSnapshot snapshot_{};
    };

} // namespace rxtech
