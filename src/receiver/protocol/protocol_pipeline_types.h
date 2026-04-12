#pragma once

#include <cstddef>
#include <cstdint>

#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/udp_payload_assembler.h"

namespace rxtech
{

    struct ProcessedPacket
    {
        UdpPayloadFrame udp_frame;
        ParsedPacketView parsed;
        InterpretedPacketView interpreted;
        std::uint32_t source_queue_id = 0;
        std::uint64_t source_ts_ns = 0;
    };

    struct PacketProcessStats
    {
        std::uint64_t accepted_bytes = 0;
        std::size_t accepted_packets = 0;
        std::uint64_t filtered_packets = 0;
    };

    struct DatagramParseResult
    {
        PacketProcessStats stats;
        bool has_packet = false;
        ProcessedPacket processed;
    };

} // namespace rxtech
