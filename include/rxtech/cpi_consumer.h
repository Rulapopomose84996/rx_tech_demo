#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "rxtech/cpi_finalizer.h"
#include "rxtech/spsc_ring.h"

namespace rxtech
{

    /// Callback invoked by the consumer for each CpiOutput.
    using CpiOutputHandler = std::function<void(const CpiOutput &)>;

    /// Consumer loop that drains the output SPSC ring, processes each CpiOutput,
    /// and pushes a ReleaseToken back through the recycle ring.
    class CpiConsumer
    {
    public:
        CpiConsumer(SpscRing<CpiOutput> &output_ring,
                    SpscRing<ReleaseToken> &recycle_ring,
                    CpiOutputHandler handler)
            : output_ring_(output_ring),
              recycle_ring_(recycle_ring),
              handler_(std::move(handler)) {}

        /// Run the consumer loop (blocks until stop() is called).
        void run(const std::atomic<bool> &stop_flag);

        /// How many CpiOutputs were processed.
        std::uint64_t processed_count() const noexcept { return processed_count_; }

    private:
        SpscRing<CpiOutput> &output_ring_;
        SpscRing<ReleaseToken> &recycle_ring_;
        CpiOutputHandler handler_;
        std::uint64_t processed_count_ = 0;
    };

} // namespace rxtech
