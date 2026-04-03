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
    ctx->header.channels_per_prt = 3U;
    ctx->header.packets_per_channel = 9U;

    rxtech::CpiFinalizer finalizer;
    ctx->header.state = rxtech::CpiState::ACTIVE;
    ctx->header.ready_prt_count = 1U;
    const auto maybe_output = finalizer.try_finalize(*ctx, rxtech::TriggerCpiSwitch);
    assert(maybe_output.has_value());
    assert(ctx->header.state == rxtech::CpiState::SEALED);

    rxtech::CpiOutput output = *maybe_output;
    output.pool_index = idx;
    output.output_id = 1U;
    assert(output_ring.push(output));

    // Start consumer thread.
    std::vector<std::uint16_t> consumed_cpi_ids;
    std::atomic<bool> stop{false};
    rxtech::CpiConsumer consumer(output_ring, recycle_ring,
                                  [&](const rxtech::CpiOutput &out) {
                                      consumed_cpi_ids.push_back(out.cpi_id);
                                  });
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
        constexpr std::uint16_t kCount = 4U; // pool depth
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
            c->header.channels_per_prt = 3U;
            c->header.packets_per_channel = 9U;
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
        rxtech::CpiConsumer consumer2(out2, rec2,
                                       [&](const rxtech::CpiOutput &o) {
                                           ids.push_back(o.cpi_id);
                                       });
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

    return 0;
}
