#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "rxtech/cpi_context.h"

namespace rxtech
{

    constexpr std::size_t kCpiContextPoolDepth = 16U;

    class CpiContextPool
    {
      public:
        CpiContextPool() : pool_(std::make_unique<CpiContext[]>(kCpiContextPoolDepth))
        {
            for (std::size_t index = 0; index < kCpiContextPoolDepth; ++index)
            {
                pool_[index].reset(0U, static_cast<std::uint32_t>(index));
                pool_[index].header.state = CpiState::RECYCLED;
                in_use_[index].store(false, std::memory_order_relaxed);
            }
        }

        std::uint32_t acquire(std::uint16_t cpi_id)
        {
            for (std::size_t index = 0; index < in_use_.size(); ++index)
            {
                bool expected = false;
                if (in_use_[index].compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                           std::memory_order_relaxed))
                {
                    pool_[index].reset(cpi_id, static_cast<std::uint32_t>(index));
                    return static_cast<std::uint32_t>(index);
                }
            }
            return kInvalidPoolIndex;
        }

        void release(std::uint32_t index)
        {
            if (index >= kCpiContextPoolDepth)
            {
                return;
            }
            pool_[index].reset(0U, index);
            pool_[index].header.state = CpiState::RECYCLED;
            in_use_[index].store(false, std::memory_order_release);
        }

        CpiContext *get(std::uint32_t index)
        {
            return index < kCpiContextPoolDepth ? &pool_[index] : nullptr;
        }

        const CpiContext *get(std::uint32_t index) const
        {
            return index < kCpiContextPoolDepth ? &pool_[index] : nullptr;
        }

      private:
        std::unique_ptr<CpiContext[]> pool_;
        std::array<std::atomic<bool>, kCpiContextPoolDepth> in_use_{};
    };

} // namespace rxtech
