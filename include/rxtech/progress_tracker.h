#pragma once

#include <cstdint>

#include "rxtech/cpi_context.h"
#include "rxtech/protocol_spec.h"

namespace rxtech
{

    class ProgressTracker
    {
      public:
        ProgressTracker() = default;
        explicit ProgressTracker(const ProtocolSpec &spec) : spec_(spec) {}

        void advance(CpiContext &ctx, std::uint16_t prt, std::uint16_t channel, bool saw_tail = false) const noexcept;

      private:
        ProtocolSpec spec_{};
    };

} // namespace rxtech
