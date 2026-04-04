#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>

#include "rxtech/slow_consumer.h"

int main()
{
    rxtech::SpscRing<rxtech::CpiOutput> output_ring(4U);
    rxtech::SpscRing<rxtech::ReleaseToken> recycle_ring(4U);

    bool handler_called = false;
    rxtech::SlowConsumerConfig config;
    config.handler = [&](const rxtech::CpiOutput &output) {
        handler_called = true;
        assert(output.output_id == 7U);
    };

    rxtech::SlowConsumerHarness harness(output_ring, recycle_ring, config);

    rxtech::CpiOutput output;
    output.output_id = 7U;
    output.pool_index = 1U;
    assert(output_ring.push(output));

    std::atomic<bool> stop_flag{true};
    harness.run(stop_flag);

    assert(handler_called);
    rxtech::ReleaseToken token;
    assert(recycle_ring.pop(token));
    assert(token.output_id == 7U);
    assert(token.ctx_pool_index == 1U);
    assert(harness.stats().processed_count.load(std::memory_order_acquire) == 1U);
    return 0;
}
