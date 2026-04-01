#pragma once

#include "rxtech/cpi_context.h"
#include "rxtech/parser.h"
#include "rxtech/packet_desc.h"

namespace rxtech {

struct SlotWriteResult {
    bool first_write = false;
    bool duplicate = false;
    std::uint32_t slot_index = 0;
};

class SlotWriter {
public:
    SlotWriteResult write(CpiContext& context,
                          const ParsedPacketMeta& parsed,
                          const PacketDesc& packet) const;
};

}  // namespace rxtech
