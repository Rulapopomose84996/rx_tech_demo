#include "rxtech/cpi_finalizer.h"

namespace rxtech {

const char* cpi_decision_name(CpiDecision decision) noexcept {
    switch (decision) {
        case CpiDecision::complete_ok:
            return "complete_ok";
        case CpiDecision::incomplete_but_committable:
            return "incomplete_but_committable";
        case CpiDecision::abnormal_cutoff_commit:
            return "abnormal_cutoff_commit";
        case CpiDecision::discard_invalid:
            return "discard_invalid";
    }
    return "unknown";
}

FinalizeResult CpiFinalizer::finalize(CpiContext& context, FinalizeTrigger trigger) const {
    FinalizeResult result;
    context.sealed = true;
    result.sealed = true;
    if (context.expected_frag_count >= context.received_fragments) {
        result.missing_fragments = static_cast<std::uint32_t>(context.expected_frag_count - context.received_fragments);
    }

    if (context.received_fragments == 0U) {
        result.decision = CpiDecision::discard_invalid;
        return result;
    }

    if (context.received_fragments == context.expected_frag_count &&
        context.received_payload_bytes == context.expected_block_bytes) {
        result.decision = CpiDecision::complete_ok;
        return result;
    }

    if (trigger == FinalizeTrigger::switch_active || trigger == FinalizeTrigger::stop_requested) {
        result.decision = CpiDecision::abnormal_cutoff_commit;
        return result;
    }

    result.decision = CpiDecision::incomplete_but_committable;
    return result;
}

}  // namespace rxtech
