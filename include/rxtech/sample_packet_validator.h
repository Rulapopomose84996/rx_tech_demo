#pragma once

#include "rxtech/sample_packet_parser.h"

namespace rxtech {

struct PacketValidity {
    bool ok = false;
    RejectReason reason = RejectReason::none;
};

class PacketValidator {
public:
    PacketValidator() = default;
    explicit PacketValidator(const ProtocolSpec& spec) : spec_(spec) {
    }

    PacketValidity validate(const ParsedPacketView& packet) const noexcept;

private:
    ProtocolSpec spec_{};
};

}  // namespace rxtech
