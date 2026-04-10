#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "rxtech/metrics.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/sample_packet_parser.h"

namespace rxtech
{

    struct CapturedPacket
    {
        std::vector<std::uint8_t> payload;
        std::uint16_t cpi = 0;
        std::uint16_t channel = 0;
        std::uint16_t prt = 0;
        std::uint16_t packet_index = 0;
        std::string packet_kind;
        bool valid = false;
    };

    struct ProtocolCpiStats
    {
        std::uint64_t data_packets = 0;
        std::unordered_set<std::uint16_t> prts;
    };

    struct ProtocolChannelStats
    {
        std::uint64_t data_packets = 0;
        std::uint64_t iq_count = 0;
    };

    using ProtocolPrtCoverage = std::map<std::uint16_t, std::unordered_set<std::uint16_t>>;
    using CpiPrtCoverageMap = std::map<std::pair<std::uint64_t, std::uint16_t>, ProtocolPrtCoverage>;

    struct OwnerLoopRuntimeState
    {
        std::string run_status = "success";
        std::string run_error;
        std::uint64_t filtered_packets = 0;
        std::uint64_t final_tail_packets = 0;
        std::uint16_t latest_data_cpi = 0;
        std::uint16_t latest_data_prt = 0;
        bool latest_data_seen = false;
        std::unordered_set<std::uint16_t> unique_cpis;
        std::unordered_set<std::uint16_t> unique_prts;
        std::unordered_set<std::uint16_t> unique_channels;
        std::map<std::uint16_t, ProtocolChannelStats> channel_stats;
        std::map<std::uint64_t, ProtocolCpiStats> cpi_stats;
        CpiPrtCoverageMap prt_coverage;

        void record_protocol_packet(const InterpretedPacketView &packet)
        {
            unique_cpis.insert(packet.cpi);
        }

        void record_captured_packet(const InterpretedPacketView &packet)
        {
            if (packet.kind == PacketKind::data_packet)
            {
                unique_prts.insert(packet.prt);
                unique_channels.insert(packet.channel);
            }
        }

        void record_data_packet(const ParsedPacketView &parsed, const InterpretedPacketView &packet,
                                const ProtocolSpec &spec)
        {
            ProtocolChannelStats &per_channel = channel_stats[packet.channel];
            per_channel.data_packets += 1U;
            per_channel.iq_count += packet.iq_count;

            ProtocolCpiStats &per_cpi = cpi_stats[packet.cpi];
            per_cpi.data_packets += 1U;
            per_cpi.prts.insert(packet.prt);

            prt_coverage[{packet.cpi, packet.prt}][packet.channel].insert(packet.packet_index);
            latest_data_cpi = packet.cpi;
            latest_data_prt = packet.prt;
            latest_data_seen = true;
            if (parsed.tail == spec.magic_tail)
            {
                ++final_tail_packets;
            }
        }

        void populate_common_summary(RunSummary &summary) const
        {
            summary.run.status = run_status;
            summary.run.error_message = run_error;
            summary.backend.filtered_packets = filtered_packets;
            summary.protocol.cpi_count = static_cast<std::uint64_t>(unique_cpis.size());
            summary.protocol.prt_count = static_cast<std::uint64_t>(unique_prts.size());
            summary.protocol.channel_count = static_cast<std::uint64_t>(unique_channels.size());
            summary.protocol.final_tail_packets = final_tail_packets;
        }

        void apply_output_degradation(bool drop_is_error)
        {
            if (run_status == "success" || (run_status == "degraded" && drop_is_error))
            {
                run_status = drop_is_error ? "error" : "degraded";
            }
        }

        void populate_protocol_summaries(RunSummary &summary) const
        {
            for (const auto &entry : channel_stats)
            {
                ProtocolChannelSummary channel_summary;
                channel_summary.channel = entry.first;
                channel_summary.data_packets = entry.second.data_packets;
                channel_summary.iq_count = entry.second.iq_count;
                summary.protocol.channels.push_back(channel_summary);
            }

            for (const auto &entry : cpi_stats)
            {
                ProtocolCpiSummary cpi_summary;
                cpi_summary.cpi = entry.first;
                cpi_summary.data_packets = entry.second.data_packets;
                cpi_summary.prt_count = static_cast<std::uint64_t>(entry.second.prts.size());
                summary.protocol.cpis.push_back(cpi_summary);
            }
        }
    };

} // namespace rxtech
