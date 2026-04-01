#pragma once

#include <cstdint>

#include "rxtech/cpi_context.h"

namespace rxtech {

enum class FinalizeTrigger {
    full_ready,
    switch_active,
    stop_requested
};

enum class CpiDecision {
    complete_ok,
    incomplete_but_committable,
    abnormal_cutoff_commit,
    discard_invalid
};

struct FinalizeResult {
    bool sealed = false;
    CpiDecision decision = CpiDecision::discard_invalid;
    std::uint32_t missing_fragments = 0;
};

class CpiFinalizer {
public:
    FinalizeResult finalize(CpiContext& context, FinalizeTrigger trigger) const;
};

const char* cpi_decision_name(CpiDecision decision) noexcept;

}  // namespace rxtech
