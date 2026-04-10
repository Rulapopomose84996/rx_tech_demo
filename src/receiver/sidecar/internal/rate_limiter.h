#pragma once

#include <cstdint>

namespace rxtech
{

    struct RateLimitedEventState
    {
        std::uint64_t total_count = 0;
        std::uint64_t suppressed_count = 0;
        std::uint64_t next_emit_after_ns = 0;
        bool emitted_once = false;
    };

    struct RateLimitedEventDecision
    {
        bool emit_detail = false;
        bool emit_summary = false;
        std::uint64_t suppressed_count = 0;
        std::uint64_t total_count = 0;
    };

    inline RateLimitedEventDecision observe_rate_limited_event(RateLimitedEventState &state, std::uint64_t now_ns,
                                                               std::uint64_t interval_ns) noexcept
    {
        ++state.total_count;

        if (!state.emitted_once)
        {
            state.emitted_once = true;
            state.next_emit_after_ns = now_ns + interval_ns;
            return {true, false, 0U, state.total_count};
        }

        if (now_ns >= state.next_emit_after_ns)
        {
            const std::uint64_t suppressed_count = state.suppressed_count;
            state.suppressed_count = 0U;
            state.next_emit_after_ns = now_ns + interval_ns;
            return {true, suppressed_count > 0U, suppressed_count, state.total_count};
        }

        ++state.suppressed_count;
        return {false, false, 0U, state.total_count};
    }

} // namespace rxtech
