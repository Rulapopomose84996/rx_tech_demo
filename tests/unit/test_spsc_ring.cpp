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
    // Basic push/pop
    {
        rxtech::SpscRing<int> ring(2U);
        assert(ring.push(1));
        assert(ring.push(2));
        assert(!ring.push(3)); // full (capacity rounds to 2)

        int value = 0;
        assert(ring.pop(value));
        assert(value == 1);
        assert(ring.pop(value));
        assert(value == 2);
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

        std::thread producer([&] {
            for (std::uint64_t i = 1; i <= kCount; ++i)
            {
                while (!ring.push(i)) {}
                sum_produced += i;
            }
            done.store(true, std::memory_order_release);
        });

        std::thread consumer([&] {
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
            }
        });

        producer.join();
        consumer.join();
        assert(sum_produced == sum_consumed);
    }

    return 0;
}
