#pragma once

#include <string>

#include "rxtech/sample_packet_parser.h"

namespace rxtech {

struct SamplePacketValidation {
    bool ok = false;
    std::string reason;
};

class SamplePacketValidator {
public:
    SamplePacketValidation validate(const SamplePacketView& packet) const noexcept;
};

}  // namespace rxtech
