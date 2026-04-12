#include "rxtech/cpi_consumer.h"

#include <emmintrin.h>

namespace rxtech
{

    void CpiConsumer::run(const std::atomic<bool> &stop_flag)
    {
        CpiOutput output;
        while (!stop_flag.load(std::memory_order_relaxed))
        {
            if (!output_ring_.pop(output))
            {
                _mm_pause();
                continue;
            }

            // Invoke the user-supplied handler (copy data, forward via IPC, etc.)
            if (handler_)
            {
                handler_(output);
            }

            // Return the pool slot to the owner thread via recycle ring.
            ReleaseToken token;
            token.output_id = output.output_id;
            token.ctx_pool_index = output.pool_index;
            // Spin with bounded retries — if recycle ring stays full, drop the token
            // to avoid indefinite blocking on the consumer thread.
            static constexpr int kMaxRecycleSpins = 1024;
            for (int spin = 0; spin < kMaxRecycleSpins; ++spin)
            {
                if (recycle_ring_.push(token))
                {
                    break;
                }
                if (stop_flag.load(std::memory_order_relaxed))
                {
                    break;
                }
                _mm_pause();
            }

            processed_count_.fetch_add(1U, std::memory_order_release);
        }

        // Drain remaining items before exit.
        while (output_ring_.pop(output))
        {
            if (handler_)
            {
                handler_(output);
            }
            ReleaseToken token;
            token.output_id = output.output_id;
            token.ctx_pool_index = output.pool_index;
            recycle_ring_.push(token);
            processed_count_.fetch_add(1U, std::memory_order_release);
        }
    }

} // namespace rxtech
