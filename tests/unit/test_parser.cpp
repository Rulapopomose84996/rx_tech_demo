#include <cassert>

#include "rxtech/parser.h"

int main() {
    rxtech::PacketDesc packet;
    packet.len = 128;
    const rxtech::ParsedPacketMeta meta = rxtech::parse_packet(packet);
    assert(meta.valid);
    return 0;
}
