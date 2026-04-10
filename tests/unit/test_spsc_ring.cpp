#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <atomic>
#include <cstdint>
#include <thread>

#include "rxtech/spsc_ring.h"

int main()
{
    {
        assert(rxtech::normalize_spsc_usable_capacity(0U) == 1U);
        assert(rxtech::normalize_spsc_usable_capacity(1U) == 1U);
        assert(rxtech::normalize_spsc_usable_capacity(2U) == 1U);
        assert(rxtech::normalize_spsc_usable_capacity(3U) == 3U);
        assert(rxtech::normalize_spsc_usable_capacity(32U) == 31U);
        assert(rxtech::normalize_spsc_usable_capacity(63U) == 63U);
        assert(rxtech::normalize_spsc_usable_capacity(64U) == 63U);
    }

    // Basic push/pop
    {
        rxtech::SpscRing<int> ring(2U);
        // min_capacity=2 → internal=4 (next_pow2(3)) → usable=3
        assert(ring.capacity() == 3U);
        assert(ring.push(1));
        assert(ring.push(2));
        assert(ring.push(3));  // 3rd push succeeds (usable capacity is 3)
        assert(!ring.push(4)); // 4th fails — ring is full

        int value = 0;
        assert(ring.pop(value));
        assert(value == 1);
        assert(ring.pop(value));
        assert(value == 2);
        assert(ring.pop(value));
        assert(value == 3);
        assert(!ring.pop(value)); // empty
    }

    // Wrap-around: push/pop more items than capacity
    {
        rxtech::SpscRing<int> ring(4U);
        for (int i = 0; i < 100; ++i)
        {
            assert(ring.push(i));
            int v = -1;
            assert(ring.pop(v));
            assert(v == i);
        }
    }

    // Concurrent producer/consumer correctness
    {
        constexpr std::uint64_t kCount = 100000U;
        rxtech::SpscRing<std::uint64_t> ring(64U);
        std::atomic<bool> done{false};
        std::uint64_t sum_produced = 0;
        std::uint64_t sum_consumed = 0;

        std::thread producer([&]
                             {
            for (std::uint64_t i = 1; i <= kCount; ++i)
            {
                while (!ring.push(i)) {}
                sum_produced += i;
            }
            done.store(true, std::memory_order_release); });

        std::thread consumer([&]
                             {
            std::uint64_t v = 0;
            std::uint64_t last = 0;
            while (!done.load(std::memory_order_acquire) || ring.size_approx() > 0)
            {
                if (ring.pop(v))
                {
                    assert(v == last + 1); // monotonically increasing
                    last = v;
                    sum_consumed += v;
                }
            }
            // Drain remaining
            while (ring.pop(v))
            {
                assert(v == last + 1);
                last = v;
                sum_consumed += v;
            } });

        producer.join();
        consumer.join();
        assert(sum_produced == sum_consumed);
    }

    return 0;
}
