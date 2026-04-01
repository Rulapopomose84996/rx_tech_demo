#pragma once

#include <optional>

#include "rxtech/cpi_context.h"
#include "rxtech/recent_closed_ring.h"

namespace rxtech {

struct OwnerState {
    std::optional<CpiContext> active_context;
    RecentClosedRing recent_closed;
};

}  // namespace rxtech
