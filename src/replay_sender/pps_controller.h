#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace rxtech::replay
{

    /// Token-bucket rate controller.
    ///
    /// For pps == 0 the wait is always a no-op (unlimited rate).
    /// For pps > 0 and gaps > 1ms the controller sleeps; for smaller gaps
    /// it busy-spins on a steady_clock to achieve microsecond accuracy.
    class PpsController
    {
    public:
        explicit PpsController(std::uint32_t pps)
            : ns_per_token_(pps > 0 ? 1'000'000'000ULL / pps : 0)
        {
            next_ns_ = now_ns();
        }

        /// Block until it is safe to send the next packet.
        void wait_for_next_slot() noexcept
        {
            if (ns_per_token_ == 0)
                return;

            const std::uint64_t deadline = next_ns_;
            next_ns_ += ns_per_token_;

            const std::uint64_t n = now_ns();
            if (n >= deadline)
                return;

            const std::uint64_t wait = deadline - n;
            if (wait > 1'000'000ULL) // > 1 ms: sleep
            {
                std::this_thread::sleep_for(std::chrono::nanoseconds(wait - 500'000ULL));
            }
            // Busy spin for final microseconds
            while (now_ns() < deadline)
            {
                // spin
            }
        }

    private:
        static std::uint64_t now_ns() noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }

        std::uint64_t ns_per_token_;
        std::uint64_t next_ns_;
    };

} // namespace rxtech::replay
