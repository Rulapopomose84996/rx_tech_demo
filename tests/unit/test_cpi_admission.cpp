#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "rxtech/cpi_admission.h"

int main()
{
    rxtech::CpiAdmission admission;
    rxtech::RecentClosedRing closed;

    rxtech::ParsedPacketView packet;
    packet.valid = true;
    packet.cpi = 10U;

    assert(admission.judge(packet, 10U, closed) == rxtech::AdmissionResult::WRITE_ACTIVE);

    packet.cpi = 11U;
    assert(admission.judge(packet, 10U, closed) == rxtech::AdmissionResult::TRIGGER_CPI_SWITCH);

    closed.push(9U, 123U, rxtech::CpiDecision::COMPLETE_OK);
    packet.cpi = 9U;
    assert(admission.judge(packet, 10U, closed) == rxtech::AdmissionResult::LATE_TO_CLOSED);

    packet.cpi = 15U;
    assert(admission.judge(packet, 10U, closed) == rxtech::AdmissionResult::DROP);
    return 0;
}
