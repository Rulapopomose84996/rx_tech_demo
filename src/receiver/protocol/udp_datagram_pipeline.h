#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>

#include "rxtech/udp_datagram.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/rx_config.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/sample_packet_validator.h"
#include "rxtech/udp_payload_assembler.h"
#include "protocol_pipeline_types.h"

namespace rxtech
{

    class UdpDatagramPipeline
    {
    public:
        UdpDatagramPipeline(const RxConfig &config, const ProtocolSpec &spec);

        PacketProcessStats process_datagram(const UdpDatagramDesc &datagram,
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
        std::uint32_t allowed_source_ipv4_be_ = 0;
        std::uint32_t receiver_ipv4_be_ = 0;
    };

} // namespace rxtech
