#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>

#include "rxtech/metrics.h"
#include "rxtech/packet_desc.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/rx_config.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/sample_packet_validator.h"
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

    class PacketPipeline
    {
    public:
        PacketPipeline(const RxConfig &config, const ProtocolSpec &spec);

        PacketProcessStats process_packet(const PacketDesc &packet,
                                          IMetricsCollector &metrics,
                                          std::ostream *diagnostic_output,
                                          std::uint32_t &invalid_dumped,
                                          const std::function<void(const ProcessedPacket &)> &on_packet);

    private:
        bool matches_packet_filter(const UdpPayloadFrame &frame) const;

        RxConfig config_;
        ProtocolSpec spec_;
        PacketParser parser_;
        PacketValidator validator_;
        ProtocolSequenceInterpreter interpreter_;
        UdpPayloadAssembler assembler_;
        std::uint32_t allowed_source_ipv4_be_ = 0;
        std::uint32_t receiver_ipv4_be_ = 0;
    };

} // namespace rxtech
