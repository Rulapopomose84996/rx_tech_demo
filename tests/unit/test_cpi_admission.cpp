#include <cassert>

#include "rxtech/cpi_admission.h"
#include "rxtech/owner_state.h"

int main() {
    rxtech::CpiAdmission admission;
    rxtech::OwnerState owner_state;

    rxtech::ParsedPacketMeta first_packet;
    first_packet.valid = true;
    first_packet.block_id = 10U;
    auto first = admission.decide(first_packet, owner_state);
    assert(first.kind == rxtech::AdmissionKind::start_new_cpi);

    rxtech::CpiContext context;
    context.cpi_id = 10U;
    owner_state.active_context = context;

    rxtech::ParsedPacketMeta active_packet;
    active_packet.valid = true;
    active_packet.block_id = 10U;
    auto active = admission.decide(active_packet, owner_state);
    assert(active.kind == rxtech::AdmissionKind::write_active_cpi);

    rxtech::ParsedPacketMeta next_packet;
    next_packet.valid = true;
    next_packet.block_id = 11U;
    auto next = admission.decide(next_packet, owner_state);
    assert(next.kind == rxtech::AdmissionKind::switch_active_cpi);

    owner_state.recent_closed.push(9U);
    rxtech::ParsedPacketMeta late_packet;
    late_packet.valid = true;
    late_packet.block_id = 9U;
    auto late = admission.decide(late_packet, owner_state);
    assert(late.kind == rxtech::AdmissionKind::late_to_closed_cpi);

    rxtech::ParsedPacketMeta stale_packet;
    stale_packet.valid = true;
    stale_packet.block_id = 8U;
    auto stale = admission.decide(stale_packet, owner_state);
    assert(stale.kind == rxtech::AdmissionKind::stale_cpi);

    return 0;
}
