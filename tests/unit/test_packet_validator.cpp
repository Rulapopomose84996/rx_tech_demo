#include <cassert>

#include "rxtech/packet_validator.h"

int main() {
    rxtech::PacketValidator validator;

    rxtech::ParsedPacketMeta invalid_meta;
    invalid_meta.valid = false;
    invalid_meta.error_reason = "invalid magic";
    const rxtech::PacketValidation invalid = validator.validate(invalid_meta);
    assert(!invalid.ok);
    assert(invalid.reason == "invalid magic");

    rxtech::ParsedPacketMeta valid_meta;
    valid_meta.valid = true;
    valid_meta.block_bytes = 1024;
    valid_meta.frag_count = 2;
    valid_meta.frag_payload_bytes = 512;
    const rxtech::PacketValidation valid = validator.validate(valid_meta);
    assert(valid.ok);
    assert(valid.reason.empty());

    rxtech::ParsedPacketMeta empty_payload_meta;
    empty_payload_meta.valid = true;
    empty_payload_meta.block_bytes = 1024;
    empty_payload_meta.frag_count = 2;
    empty_payload_meta.frag_payload_bytes = 0;
    const rxtech::PacketValidation empty_payload = validator.validate(empty_payload_meta);
    assert(!empty_payload.ok);
    assert(empty_payload.reason == "empty fragment payload");

    return 0;
}
