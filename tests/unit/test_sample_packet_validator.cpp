#include <iostream>
#include <vector>

#include "rxtech/sample_packet_parser.h"
#include "rxtech/sample_packet_validator.h"
#include "rxtech/udp_payload_assembler.h"

int main() {
    rxtech::SamplePacketParser parser;
    rxtech::SamplePacketValidator validator;

    rxtech::SamplePacketView packet;
    packet.valid = true;
    packet.kind = rxtech::SamplePacketKind::data_packet;
    packet.channel = 3U;
    packet.prt = 31U;
    packet.packet_index = 9U;
    packet.payload_len = 2032U;
    packet.tail = 0U;
    packet.ip_fragment_offset = 0U;
    packet.more_ip_fragments = false;

    const rxtech::SamplePacketValidation valid = validator.validate(packet);
    if (!valid.ok || !valid.reason.empty()) {
        std::cerr << "expected packet_index=9 on channel=3 to be accepted, got ok=" << valid.ok
                  << " reason=" << valid.reason << '\n';
        return 1;
    }

    packet.packet_index = 0U;
    const rxtech::SamplePacketValidation packet_index_out_of_range = validator.validate(packet);
    if (packet_index_out_of_range.ok || packet_index_out_of_range.reason != "packet index out of range") {
        std::cerr << "expected packet_index=0 to be rejected, got ok=" << packet_index_out_of_range.ok
                  << " reason=" << packet_index_out_of_range.reason << '\n';
        return 1;
    }

    packet.packet_index = 9U;
    packet.payload_len = 2031U;
    const rxtech::SamplePacketValidation payload_length_invalid = validator.validate(packet);
    if (payload_length_invalid.ok || payload_length_invalid.reason != "unexpected data payload length") {
        std::cerr << "expected payload_len=2031 to be rejected, got ok=" << payload_length_invalid.ok
                  << " reason=" << payload_length_invalid.reason << '\n';
        return 1;
    }

    packet.payload_len = 2032U;
    packet.more_ip_fragments = true;
    const rxtech::SamplePacketValidation fragmented = validator.validate(packet);
    if (fragmented.ok || fragmented.reason != "data packet is fragmented") {
        std::cerr << "expected fragmented packet to be rejected, got ok=" << fragmented.ok
                  << " reason=" << fragmented.reason << '\n';
        return 1;
    }

    rxtech::UdpPayloadFrame control_table_frame;
    control_table_frame.udp_payload = std::vector<std::uint8_t>(
        {0x00, 0xFF, 0xAA, 0x55, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    control_table_frame.udp_payload.resize(2048U, 0x00U);

    const rxtech::SamplePacketView control_table_packet = parser.parse(control_table_frame);
    const rxtech::SamplePacketValidation control_table_valid = validator.validate(control_table_packet);
    if (!control_table_valid.ok || !control_table_valid.reason.empty()) {
        std::cerr << "expected control table from UDP payload frame to be accepted, got ok="
                  << control_table_valid.ok << " reason=" << control_table_valid.reason << '\n';
        return 1;
    }

    return 0;
}
