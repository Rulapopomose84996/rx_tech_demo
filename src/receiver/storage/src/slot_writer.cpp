#include "rxtech/slot_writer.h"

#include <stdexcept>

namespace rxtech {

SlotWriteResult SlotWriter::write(CpiContext& context,
                                  const ParsedPacketMeta& parsed,
                                  const PacketDesc& packet) const {
    if (parsed.frag_idx >= context.slot_received.size()) {
        throw std::runtime_error("slot index is out of range");
    }

    SlotWriteResult result;
    result.slot_index = parsed.frag_idx;
    if (context.slot_received[parsed.frag_idx]) {
        result.duplicate = true;
        return result;
    }

    context.slot_received[parsed.frag_idx] = true;
    context.slot_payloads[parsed.frag_idx].assign(packet.data + parsed.payload_offset,
                                                  packet.data + parsed.payload_offset + parsed.frag_payload_bytes);
    context.received_fragments += 1U;
    context.received_payload_bytes += parsed.frag_payload_bytes;
    result.first_write = true;
    return result;
}

}  // namespace rxtech
