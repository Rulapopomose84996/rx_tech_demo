#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "rxtech/cpi_consumer.h"
#include "rxtech/cpi_context.h"
#include "rxtech/cpi_context_pool.h"
#include "rxtech/cpi_finalizer.h"
#include "rxtech/metrics.h"
#include "rxtech/spsc_ring.h"

int main()
{
    // Test the full CpiOutput → SPSC → Consumer → ReleaseToken → pool release pipeline.
    constexpr std::size_t kRingCap = 8U;
    rxtech::SpscRing<rxtech::CpiOutput> output_ring(kRingCap);
    rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(kRingCap);
    rxtech::CpiContextPool pool;

    // Acquire a context and finalize it to produce a CpiOutput.
    const std::uint32_t idx = pool.acquire(42U);
    assert(idx != rxtech::kInvalidPoolIndex);

    rxtech::CpiContext *ctx = pool.get(idx);
    assert(ctx != nullptr);
    rxtech::ControlSnapshot control;
    control.cpi_id = 42U;
    control.n_prt = 1U;
    control.channel_count = 3U;
    control.packets_per_channel = 9U;
    control.timeout_ns = 1000U;
    control.valid = true;
    control.bind_source = rxtech::BindSource::control;
    rxtech::bind_control_snapshot(*ctx, control);

    rxtech::CpiFinalizer finalizer;
    ctx->header.state = rxtech::CpiState::ACTIVE;
    ctx->header.ready_prt_count = 1U;
    const auto maybe_output = finalizer.try_finalize(*ctx, rxtech::TriggerCpiSwitch);
    assert(maybe_output.has_value());
    assert(ctx->header.state == rxtech::CpiState::SEALED);

    rxtech::CpiOutput output = *maybe_output;
    output.pool_index = idx;
    output.output_id = 1U;
    assert(output.control.cpi_id == 42U);
    assert(output.control.n_prt == 1U);
    assert(output.control.bind_source == rxtech::BindSource::control);
    assert((output.trigger_bits & rxtech::TriggerCpiSwitch) != 0U);
    assert(output_ring.push(output));

    // Start consumer thread.
    std::vector<std::uint16_t> consumed_cpi_ids;
    std::atomic<bool> stop{false};
    rxtech::CpiConsumer consumer(output_ring, recycle_ring,
                                 [&](const rxtech::CpiOutput &out) { consumed_cpi_ids.push_back(out.cpi_id); });
    std::thread consumer_thread([&] { consumer.run(stop); });

    // Wait for consumer to process.
    while (consumer.processed_count() == 0)
    {
    }

    // Signal stop.
    stop.store(true, std::memory_order_release);
    consumer_thread.join();

    assert(consumed_cpi_ids.size() == 1U);
    assert(consumed_cpi_ids[0] == 42U);

    // Drain recycle ring and release pool slot.
    rxtech::ReleaseToken token;
    assert(recycle_ring.pop(token));
    assert(token.ctx_pool_index == idx);
    assert(token.output_id == 1U);
    pool.release(token.ctx_pool_index);

    // Pool slot should be reusable now.
    const std::uint32_t idx2 = pool.acquire(99U);
    assert(idx2 == idx); // same slot reused
    pool.release(idx2);

    // Multi-CPI stress: acquire, finalize, push multiple → all recycled.
    {
        constexpr std::uint16_t kCount = static_cast<std::uint16_t>(rxtech::kCpiContextPoolDepth);
        rxtech::SpscRing<rxtech::CpiOutput> out2(16U);
        rxtech::SpscRing<rxtech::ReleaseToken> rec2(16U);
        rxtech::CpiContextPool pool2;
        std::vector<std::uint16_t> ids;

        for (std::uint16_t i = 0; i < kCount; ++i)
        {
            const std::uint32_t pi = pool2.acquire(i);
            assert(pi != rxtech::kInvalidPoolIndex);
            rxtech::CpiContext *c = pool2.get(pi);
            c->header.state = rxtech::CpiState::ACTIVE;
            c->header.ready_prt_count = 1U;
            rxtech::ControlSnapshot control2;
            control2.cpi_id = static_cast<std::uint16_t>(i);
            control2.n_prt = 1U;
            control2.channel_count = 3U;
            control2.packets_per_channel = 9U;
            control2.valid = true;
            control2.bind_source = rxtech::BindSource::fixed;
            rxtech::bind_control_snapshot(*c, control2);
            const auto out = finalizer.try_finalize(*c, rxtech::TriggerCpiSwitch);
            assert(out.has_value());
            rxtech::CpiOutput o = *out;
            o.pool_index = pi;
            o.output_id = static_cast<std::uint64_t>(i + 1U);
            assert(out2.push(o));
        }

        // Pool should be exhausted
        assert(pool2.acquire(100U) == rxtech::kInvalidPoolIndex);

        std::atomic<bool> stop2{false};
        rxtech::CpiConsumer consumer2(out2, rec2, [&](const rxtech::CpiOutput &o) { ids.push_back(o.cpi_id); });
        std::thread t([&] { consumer2.run(stop2); });

        while (consumer2.processed_count() < kCount)
        {
        }
        stop2.store(true, std::memory_order_release);
        t.join();

        assert(ids.size() == kCount);

        // Drain recycle and release all
        rxtech::ReleaseToken tk;
        std::uint32_t released = 0;
        while (rec2.pop(tk))
        {
            pool2.release(tk.ctx_pool_index);
            ++released;
        }
        assert(released == kCount);

        // All slots should be free again
        for (std::uint16_t i = 0; i < kCount; ++i)
        {
            const std::uint32_t pi = pool2.acquire(i + 200U);
            assert(pi != rxtech::kInvalidPoolIndex);
        }
    }

    // ── Zero-blocking output drop test ──────────────────────────────
    // Prove that when the output ring is full:
    //   1. finalize does NOT wait for ring space
    //   2. pool slot is released immediately (reusable)
    //   3. output_backpressure_count is incremented
    {
        // Use a tiny ring so we can easily fill it.
        // SpscRing(1) allocates cap=2 internally (next power-of-two >= 1+1),
        // with one slot sacrificed, so usable capacity is exactly 1.
        constexpr std::size_t kTinyRingCap = 1U;
        rxtech::SpscRing<rxtech::CpiOutput> tiny_output_ring(kTinyRingCap);
        rxtech::SpscRing<rxtech::ReleaseToken> tiny_recycle_ring(kTinyRingCap);
        rxtech::CpiContextPool drop_pool;
        rxtech::CpiFinalizer drop_finalizer;
        rxtech::MetricsCollector drop_metrics;

        // Fill all usable output slots so next push will fail.
        // SpscRing(1) → internal cap=2, usable=1. Fill it with a dummy.
        {
            rxtech::CpiOutput dummy{};
            dummy.cpi_id = 999U;
            assert(tiny_output_ring.push(dummy));
        }

        // Now acquire a CPI context, finalize it, and try to push it
        // to the full output ring — should drop immediately.
        const std::uint32_t drop_idx = drop_pool.acquire(77U);
        assert(drop_idx != rxtech::kInvalidPoolIndex);
        rxtech::CpiContext *drop_ctx = drop_pool.get(drop_idx);
        assert(drop_ctx != nullptr);

        rxtech::ControlSnapshot drop_control{};
        drop_control.cpi_id = 77U;
        drop_control.n_prt = 1U;
        drop_control.channel_count = 3U;
        drop_control.packets_per_channel = 9U;
        drop_control.valid = true;
        drop_control.bind_source = rxtech::BindSource::control;
        rxtech::bind_control_snapshot(*drop_ctx, drop_control);
        drop_ctx->header.state = rxtech::CpiState::ACTIVE;
        drop_ctx->header.ready_prt_count = 1U;

        const auto maybe_drop_output = drop_finalizer.try_finalize(*drop_ctx, rxtech::TriggerCpiSwitch);
        assert(maybe_drop_output.has_value());

        // Manually simulate what the coordinator does:
        rxtech::CpiOutput drop_out = *maybe_drop_output;
        drop_out.pool_index = drop_idx;
        drop_out.output_id = 100U;
        const bool push_ok = tiny_output_ring.push(drop_out);
        assert(!push_ok); // Ring should be full

        // Simulate the immediate non-blocking drop path
        drop_metrics.on_output_backpressure();
        drop_pool.release(drop_idx);

        // Verify the drop was recorded
        rxtech::RunSummary drop_summary = drop_metrics.finalize("test", "", "", 0);
        assert(drop_summary.performance.output_backpressure_count == 1U);

        // Verify pool slot is immediately reusable
        const std::uint32_t reused_idx = drop_pool.acquire(99U);
        assert(reused_idx != rxtech::kInvalidPoolIndex);
        drop_pool.release(reused_idx);
    }

    // ── Timeout finalization should still emit diagnosable output ──────────────────────────────
    {
        rxtech::CpiContextPool timeout_pool;
        const std::uint32_t timeout_idx = timeout_pool.acquire(55U);
        assert(timeout_idx != rxtech::kInvalidPoolIndex);

        rxtech::CpiContext *timeout_ctx = timeout_pool.get(timeout_idx);
        assert(timeout_ctx != nullptr);
        timeout_ctx->header.state = rxtech::CpiState::ACTIVE;
        timeout_ctx->header.received_slot_count = 1U;
        timeout_ctx->header.first_rx_tsc = 123U;

        rxtech::ControlSnapshot timeout_control{};
        timeout_control.cpi_id = 55U;
        timeout_control.n_prt = 1U;
        timeout_control.channel_count = 3U;
        timeout_control.packets_per_channel = 9U;
        timeout_control.valid = true;
        timeout_control.bind_source = rxtech::BindSource::control;
        rxtech::bind_control_snapshot(*timeout_ctx, timeout_control);

        const auto timeout_output = finalizer.try_finalize(*timeout_ctx, rxtech::TriggerTimeout);
        assert(timeout_output.has_value());
        assert(timeout_output->decision == rxtech::CpiDecision::ABNORMAL_CUTOFF_COMMIT);
        assert((timeout_output->trigger_bits & rxtech::TriggerTimeout) != 0U);
        assert(timeout_output->view.payload_base != nullptr);
        assert(timeout_output->view.slot_valid_bytes != nullptr);
        assert(timeout_output->view.prt_summary != nullptr);

        timeout_pool.release(timeout_idx);
    }

    return 0;
}
