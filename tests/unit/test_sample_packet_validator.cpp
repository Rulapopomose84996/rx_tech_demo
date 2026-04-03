#include <cstdint>
#include <iostream>
#include <vector>

#include "rxtech/sample_packet_parser.h"
#include "rxtech/sample_packet_validator.h"
#include "rxtech/udp_payload_assembler.h"

int main()
{
    rxtech::PacketParser parser;
    rxtech::PacketValidator validator;
    std::vector<std::uint8_t> payload(2032U, 0xABU);

    rxtech::ParsedPacketView packet;
    packet.valid = true;
    packet.kind = rxtech::PacketKind::data_packet;
    packet.channel = 2U;
    packet.prt = 31U;
    packet.packet_index = 9U;
    packet.payload_len = 2032U;
    packet.payload_ptr = payload.data();
    packet.tail = 0U;

    const rxtech::PacketValidity valid = validator.validate(packet);
    if (!valid.ok || valid.reason != rxtech::RejectReason::none)
    {
        std::cerr << "expected packet_index=9 on channel=3 to be accepted, got ok=" << valid.ok
                  << " reason=" << rxtech::reject_reason_name(valid.reason) << '\n';
        return 1;
    }

    packet.packet_index = 0U;
    const rxtech::PacketValidity packet_index_out_of_range = validator.validate(packet);
    if (packet_index_out_of_range.ok || packet_index_out_of_range.reason != rxtech::RejectReason::invalid_packet_index)
    {
        std::cerr << "expected packet_index=0 to be rejected, got ok=" << packet_index_out_of_range.ok
                  << " reason=" << rxtech::reject_reason_name(packet_index_out_of_range.reason) << '\n';
        return 1;
    }

    packet.packet_index = 9U;
    packet.payload_len = 2031U;
    const rxtech::PacketValidity payload_length_invalid = validator.validate(packet);
    if (payload_length_invalid.ok || payload_length_invalid.reason != rxtech::RejectReason::invalid_len)
    {
        std::cerr << "expected payload_len=2031 to be rejected, got ok=" << payload_length_invalid.ok
                  << " reason=" << rxtech::reject_reason_name(payload_length_invalid.reason) << '\n';
        return 1;
    }

    packet.payload_len = 2032U;
    packet.channel = 3U;
    const rxtech::PacketValidity invalid_channel = validator.validate(packet);
    if (invalid_channel.ok || invalid_channel.reason != rxtech::RejectReason::invalid_channel)
    {
        std::cerr << "expected channel=3 to be rejected, got ok=" << invalid_channel.ok
                  << " reason=" << rxtech::reject_reason_name(invalid_channel.reason) << '\n';
        return 1;
    }

    packet.channel = 2U;
    packet.tail = 0x12345678U;
    const rxtech::PacketValidity invalid_tail = validator.validate(packet);
    if (invalid_tail.ok || invalid_tail.reason != rxtech::RejectReason::invalid_tail)
    {
        std::cerr << "expected invalid tail to be rejected, got ok=" << invalid_tail.ok
                  << " reason=" << rxtech::reject_reason_name(invalid_tail.reason) << '\n';
        return 1;
    }

    packet.tail = 0U;

    // V-008 / C-001: tail marker on non-last packet must be rejected
    packet.tail = 0x55AAFF30U;
    packet.packet_index = 5U; // not last (9)
    const rxtech::PacketValidity tail_wrong_pos = validator.validate(packet);
    if (tail_wrong_pos.ok || tail_wrong_pos.reason != rxtech::RejectReason::invalid_field_combo)
    {
        std::cerr << "expected tail on non-last packet to be rejected with invalid_field_combo, got ok="
                  << tail_wrong_pos.ok << " reason=" << rxtech::reject_reason_name(tail_wrong_pos.reason) << '\n';
        return 1;
    }

    // V-008: tail on last packet should be accepted
    packet.packet_index = 9U;
    const rxtech::PacketValidity tail_correct_pos = validator.validate(packet);
    if (!tail_correct_pos.ok || tail_correct_pos.reason != rxtech::RejectReason::none)
    {
        std::cerr << "expected tail on last packet to be accepted, got ok=" << tail_correct_pos.ok
                  << " reason=" << rxtech::reject_reason_name(tail_correct_pos.reason) << '\n';
        return 1;
    }
    packet.tail = 0U;

    rxtech::UdpPayloadFrame control_table_frame;
    control_table_frame.udp_payload = std::vector<std::uint8_t>(
        {0x00, 0xFF, 0xAA, 0x55, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    control_table_frame.udp_payload.resize(2048U, 0x00U);

    const rxtech::ParsedPacketView control_table_packet = parser.parse(control_table_frame);
    const rxtech::PacketValidity control_table_valid = validator.validate(control_table_packet);
    if (!control_table_valid.ok || control_table_valid.reason != rxtech::RejectReason::none)
    {
        std::cerr << "expected control table from UDP payload frame to be accepted, got ok="
                  << control_table_valid.ok << " reason=" << rxtech::reject_reason_name(control_table_valid.reason) << '\n';
        return 1;
    }

    return 0;
}
