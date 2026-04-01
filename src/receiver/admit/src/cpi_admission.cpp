#include "rxtech/cpi_admission.h"

namespace rxtech {

namespace {

const char* reason_for_invalid(const ParsedPacketMeta& parsed) {
    return parsed.error_reason.empty() ? "invalid packet" : parsed.error_reason.c_str();
}

}  // namespace

const char* admission_kind_name(AdmissionKind kind) noexcept {
    switch (kind) {
        case AdmissionKind::start_new_cpi:
            return "start_new_cpi";
        case AdmissionKind::write_active_cpi:
            return "write_active_cpi";
        case AdmissionKind::switch_active_cpi:
            return "switch_active_cpi";
        case AdmissionKind::late_to_closed_cpi:
            return "late_to_closed_cpi";
        case AdmissionKind::stale_cpi:
            return "stale_cpi";
        case AdmissionKind::invalid_packet:
            return "invalid_packet";
    }
    return "unknown";
}

AdmissionResult CpiAdmission::decide(const ParsedPacketMeta& parsed, const OwnerState& owner_state) const {
    if (!parsed.valid) {
        return {AdmissionKind::invalid_packet, reason_for_invalid(parsed)};
    }
    if (!owner_state.active_context.has_value()) {
        return {AdmissionKind::start_new_cpi, "no active cpi"};
    }

    const CpiContext& active = *owner_state.active_context;
    if (parsed.block_id == active.cpi_id) {
        return {AdmissionKind::write_active_cpi, "matched active cpi"};
    }
    if (parsed.block_id > active.cpi_id) {
        return {AdmissionKind::switch_active_cpi, "newer cpi observed"};
    }
    if (owner_state.recent_closed.contains(parsed.block_id)) {
        return {AdmissionKind::late_to_closed_cpi, "packet hit recent closed cpi"};
    }
    return {AdmissionKind::stale_cpi, "packet cpi is older than active cpi"};
}

}  // namespace rxtech
