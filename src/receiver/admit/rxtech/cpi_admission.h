#pragma once

#include "rxtech/admission_result.h"
#include "rxtech/owner_state.h"
#include "rxtech/parser.h"

namespace rxtech {

class CpiAdmission {
public:
    AdmissionResult decide(const ParsedPacketMeta& parsed, const OwnerState& owner_state) const;
};

}  // namespace rxtech
