#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <utility>

#include "rxtech/udp_datagram.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/rx_config.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/sample_packet_validator.h"
#include "rxtech/udp_payload_assembler.h"
#include "protocol_pipeline_types.h"
#include "../sidecar/internal/rate_limiter.h"

namespace rxtech
{

    class UdpDatagramPipeline
    {
      public:
        UdpDatagramPipeline(const RxConfig &config, const ProtocolSpec &spec);

        DatagramParseResult prepare_datagram(const UdpDatagramDesc &datagram, MetricsCollector &metrics,
                                             std::ostream *diagnostic_output, std::uint32_t &invalid_dumped);

        template <typename Callback>
        PacketProcessStats process_datagram(const UdpDatagramDesc &datagram, MetricsCollector &metrics,
                                            std::ostream *diagnostic_output, std::uint32_t &invalid_dumped,
                                            Callback &&on_packet)
        {
            DatagramParseResult result = prepare_datagram(datagram, metrics, diagnostic_output, invalid_dumped);
            if (result.has_packet)
            {
                on_packet(result.processed);
            }
            return result.stats;
        }

      private:
        bool matches_packet_filter(const UdpPayloadFrame &frame) const;

#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
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
        std::uint64_t reject_diagnostic_interval_ns_ = 0U;
        std::array<RateLimitedEventState, kRejectReasonCount> reject_diagnostic_states_{};
#endif
    };

} // namespace rxtech
