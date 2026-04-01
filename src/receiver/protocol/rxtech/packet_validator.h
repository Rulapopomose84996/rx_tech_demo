#pragma once

#include <string>

#include "rxtech/parser.h"

namespace rxtech {

struct PacketValidation {
    bool ok = false;
    std::string reason;
};

class PacketValidator {
public:
    PacketValidation validate(const ParsedPacketMeta& parsed) const;
};

}  // namespace rxtech
