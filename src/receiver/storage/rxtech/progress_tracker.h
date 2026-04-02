#pragma once

#include <cstdint>

#include "rxtech/cpi_context.h"

namespace rxtech
{

    class ProgressTracker
    {
    public:
        void advance(CpiContext &ctx, std::uint16_t prt, std::uint16_t channel, bool saw_tail = false) const noexcept;
    };

} // namespace rxtech
