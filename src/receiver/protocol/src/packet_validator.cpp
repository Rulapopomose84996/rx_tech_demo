#include "rxtech/packet_validator.h"

namespace rxtech {

PacketValidation PacketValidator::validate(const ParsedPacketMeta& parsed) const {
    if (!parsed.valid) {
        return {false, parsed.error_reason.empty() ? "parse failed" : parsed.error_reason};
    }
    if (parsed.frag_payload_bytes == 0U) {
        return {false, "empty fragment payload"};
    }
    if (parsed.block_bytes == 0U) {
        return {false, "invalid block bytes"};
    }
    return {true, ""};
}

}  // namespace rxtech
