#include "rxtech/cpi_consumer.h"

namespace rxtech
{

    void CpiConsumer::run(const std::atomic<bool> &stop_flag)
    {
        CpiOutput output;
        while (!stop_flag.load(std::memory_order_relaxed))
        {
            if (!output_ring_.pop(output))
            {
                // Spin-yield when idle — avoid busy-wait on empty ring
                // (A future improvement could use a futex/eventfd for wakeup.)
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
            // If the recycle ring is full we still need to push — spin until space.
            while (!recycle_ring_.push(token))
            {
                if (stop_flag.load(std::memory_order_relaxed))
                {
                    break;
                }
            }

            ++processed_count_;
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
            ++processed_count_;
        }
    }

} // namespace rxtech
