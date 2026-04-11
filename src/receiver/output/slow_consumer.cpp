#include "rxtech/slow_consumer.h"

#include <chrono>
#include <thread>

#include "rxtech/time_utils.h"

namespace rxtech
{

    SlowConsumerHarness::SlowConsumerHarness(SpscRing<CpiOutput> &output_ring, SpscRing<ReleaseToken> &recycle_ring,
                                             SlowConsumerConfig config)
        : output_ring_(output_ring), recycle_ring_(recycle_ring), config_(std::move(config))
    {
    }

    void SlowConsumerHarness::run(const std::atomic<bool> &stop_flag)
    {
        CpiOutput output;

        while (!stop_flag.load(std::memory_order_relaxed))
        {
            if (!output_ring_.pop(output))
            {
                continue; // spin-yield on empty ring
            }

            // Measure and update peak backlog (approximate via how long the
            // ring has been backed up — we track occupancy by observing
            // processed - recycled delta, which approximates in-flight count).
            // Simpler proxy: just increment before processing and check peak.
            const std::uint64_t in_flight =
                stats_.processed_count.load(std::memory_order_relaxed); // items we've started but not finished
            (void)in_flight;

            // Artificial slowdown
            if (config_.consume_delay_us > 0U)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(config_.consume_delay_us));
            }

            // Compute end-to-end latency from first packet to consumer exit.
            const std::uint64_t exit_tsc = steady_clock_now_ns();
            if (output.first_rx_tsc > 0U && exit_tsc > output.first_rx_tsc)
            {
                const std::uint64_t latency = exit_tsc - output.first_rx_tsc;
                stats_.total_latency_ns.fetch_add(latency, std::memory_order_relaxed);

                // Update peak backlog proxy: latency / nominal_ns_per_output
                // We use total_latency / processed as a simple "avg latency" signal.
                const std::uint64_t cnt = stats_.processed_count.load(std::memory_order_relaxed);
                const std::uint64_t approx_backlog = (config_.consume_delay_us > 0U && cnt > 0U)
                                                         ? latency / (config_.consume_delay_us * 1000ULL + 1ULL)
                                                         : 0ULL;

                std::uint64_t prev = stats_.peak_backlog.load(std::memory_order_relaxed);
                while (approx_backlog > prev)
                {
                    if (stats_.peak_backlog.compare_exchange_weak(prev, approx_backlog, std::memory_order_relaxed,
                                                                  std::memory_order_relaxed))
                    {
                        break;
                    }
                }
            }

            // Invoke optional user callback
            if (config_.handler)
                config_.handler(output);

            // Return pool slot via recycle ring — spin until space available.
            ReleaseToken token;
            token.output_id = output.output_id;
            token.ctx_pool_index = output.pool_index;

            while (!recycle_ring_.push(token))
            {
                stats_.recycle_spin_count.fetch_add(1U, std::memory_order_relaxed);
            }

            stats_.processed_count.fetch_add(1U, std::memory_order_release);
        }

        // Drain any remaining outputs after stop is signalled.
        while (output_ring_.pop(output))
        {
            if (config_.handler)
                config_.handler(output);

            ReleaseToken token;
            token.output_id = output.output_id;
            token.ctx_pool_index = output.pool_index;
            while (!recycle_ring_.push(token))
            {
            }
            stats_.processed_count.fetch_add(1U, std::memory_order_release);
        }
    }

} // namespace rxtech
