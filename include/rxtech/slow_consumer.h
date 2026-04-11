#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "rxtech/cpi_finalizer.h"
#include "rxtech/cpi_consumer.h"
#include "rxtech/spsc_ring.h"

namespace rxtech
{

    struct SlowConsumerConfig
    {
        /// Artificial delay injected per CPI output (microseconds).
        /// 0 means no delay (still drains normally).
        std::uint64_t consume_delay_us = 0;

        /// Optional per-output callback (same semantics as CpiOutputHandler).
        CpiOutputHandler handler;
    };

    /// Statistics collected by SlowConsumerHarness (all fields are atomic).
    struct SlowConsumerStats
    {
        std::atomic<std::uint64_t> processed_count{0};

        /// Sum of nanoseconds from CpiOutput.first_rx_tsc to handler-exit tsc.
        std::atomic<std::uint64_t> total_latency_ns{0};

        /// Maximum observed ring occupancy at the point of pop (backlog high watermark).
        std::atomic<std::uint64_t> peak_backlog{0};

        /// Number of times push(ReleaseToken) had to spin (ring was full).
        std::atomic<std::uint64_t> recycle_spin_count{0};

        SlowConsumerStats() = default;
        SlowConsumerStats(const SlowConsumerStats &) = delete;
        SlowConsumerStats &operator=(const SlowConsumerStats &) = delete;
    };

    /// Drop-in slow consumer for back-pressure and stress testing.
    ///
    /// Replaces the default CpiConsumer when you want to verify that the
    /// receive pipeline does not deadlock, exhaust the context pool, or
    /// produce erroneous finalizations under a stalled consumer.
    ///
    /// Usage:
    ///   SlowConsumerConfig cfg;
    ///   cfg.consume_delay_us = 5000;  // 5 ms per CPI
    ///   SlowConsumerHarness harness(output_ring, recycle_ring, cfg);
    ///   std::thread t([&]{ harness.run(stop_flag); });
    ///
    class SlowConsumerHarness
    {
    public:
        SlowConsumerHarness(SpscRing<CpiOutput> &output_ring,
                            SpscRing<ReleaseToken> &recycle_ring,
                            SlowConsumerConfig config);

        /// Consumer loop — call from a dedicated thread.  Blocks until stop_flag is set.
        void run(const std::atomic<bool> &stop_flag);

        const SlowConsumerStats &stats() const noexcept { return stats_; }

    private:
        SpscRing<CpiOutput> &output_ring_;
        SpscRing<ReleaseToken> &recycle_ring_;
        SlowConsumerConfig config_;
        SlowConsumerStats stats_;
    };

} // namespace rxtech
