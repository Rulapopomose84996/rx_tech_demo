#pragma once

#include <optional>

#include "rxtech/cpi_output.h"

namespace rxtech
{

    class CpiFinalizer
    {
    public:
        std::optional<CpiOutput> try_finalize(CpiContext &ctx, std::uint32_t trigger) const;
    };

} // namespace rxtech
