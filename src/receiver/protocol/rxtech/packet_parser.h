#pragma once

#include "rxtech/parser.h"

namespace rxtech {

class PacketParser {
public:
    ParsedPacketMeta parse(const PacketDesc& packet) const;
};

}  // namespace rxtech
