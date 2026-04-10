#pragma once

#include <array>
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

        PacketProcessStats process_datagram(const UdpDatagramDesc &datagram, IMetricsCollector &metrics,
                                            std::ostream *diagnostic_output, std::uint32_t &invalid_dumped,
                                            const std::function<void(const ProcessedPacket &)> &on_packet);

      private:
        bool matches_packet_filter(const UdpPayloadFrame &frame) const;

#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
        struct RejectDiagnosticState
        {
            std::uint64_t total_count = 0;
            std::uint64_t suppressed_count = 0;
            std::uint64_t next_emit_after_ns = 0;
            bool emitted_once = false;
        };

        void maybe_emit_invalid_diagnostic(std::ostream &diagnostic_output, const UdpDatagramDesc &datagram,
                                           const ParsedPacketView &parsed, RejectReason reason,
                                           std::uint32_t &invalid_dumped);
#endif

        RxConfig config_;
        ProtocolSpec spec_;
        PacketParser parser_;
        PacketValidator validator_;
        ProtocolSequenceInterpreter interpreter_;
        std::uint32_t allowed_source_ipv4_be_ = 0;
        std::uint32_t receiver_ipv4_be_ = 0;
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
        std::array<RejectDiagnosticState, kRejectReasonCount> reject_diagnostic_states_{};
#endif
    };

} // namespace rxtech
