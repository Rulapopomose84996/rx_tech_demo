#pragma once

#include <array>
#include <cstddef>

#include "rxtech/cpi_output.h"

namespace rxtech
{

    constexpr std::size_t kOutputPoolDepth = 4U;

    class OutputPool
    {
    public:
        CpiOutput *acquire()
        {
            for (std::size_t index = 0; index < in_use_.size(); ++index)
            {
                if (!in_use_[index])
                {
                    in_use_[index] = true;
                    pool_[index] = {};
                    pool_[index].output_id = next_output_id_++;
                    return &pool_[index];
                }
            }
            return nullptr;
        }

        void release(const ReleaseToken &token)
        {
            for (std::size_t index = 0; index < in_use_.size(); ++index)
            {
                if (in_use_[index] && pool_[index].output_id == token.output_id &&
                    pool_[index].pool_index == token.ctx_pool_index)
                {
                    in_use_[index] = false;
                    pool_[index] = {};
                    return;
                }
            }
        }

    private:
        std::array<CpiOutput, kOutputPoolDepth> pool_{};
        std::array<bool, kOutputPoolDepth> in_use_{};
        std::uint64_t next_output_id_ = 1U;
    };

} // namespace rxtech
