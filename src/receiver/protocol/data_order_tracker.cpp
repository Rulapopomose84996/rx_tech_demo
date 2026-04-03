#include "data_order_tracker.h"

#include <sstream>

#include "owner_loop_summary.h"

namespace rxtech
{

    DataOrderTracker::DataOrderTracker(const ProtocolSpec &spec) : spec_(spec)
    {
    }

    DataOrderTracker::Cursor DataOrderTracker::build_next_expected(const InterpretedPacketView &packet) const
    {
        Cursor next;
        next.cpi = packet.cpi;
        next.prt = packet.prt;
        next.channel = packet.channel;
        next.packet_index = packet.packet_index;
        if (next.packet_index < spec_.packets_per_channel)
        {
            next.packet_index = static_cast<std::uint16_t>(next.packet_index + 1U);
            return next;
        }
        next.packet_index = 1U;
        if (next.channel + 1U < spec_.channels_per_prt)
        {
            next.channel = static_cast<std::uint16_t>(next.channel + 1U);
            return next;
        }
        next.channel = 0U;
        next.prt = static_cast<std::uint16_t>(next.prt + 1U);
        return next;
    }

    bool DataOrderTracker::matches_expected(const InterpretedPacketView &packet, const Cursor &expected)
    {
        return packet.cpi == expected.cpi &&
               packet.prt == expected.prt &&
               packet.channel == expected.channel &&
               packet.packet_index == expected.packet_index;
    }

    std::string DataOrderTracker::format_point(std::uint16_t cpi,
                                               std::uint16_t prt,
                                               std::uint16_t channel,
                                               std::uint16_t packet_index)
    {
        std::ostringstream out;
        out << "CPI " << cpi
            << " / PRT " << prt
            << " / CH " << channel
            << " / PKT " << packet_index;
        return out.str();
    }

    void DataOrderTracker::observe(const InterpretedPacketView &packet)
    {
        ++checked_packets_;
        if (!initialized_)
        {
            expected_next_ = build_next_expected(packet);
            previous_packet_ = packet;
            initialized_ = true;
            return;
        }

        if (matches_expected_ && !matches_expected(packet, expected_next_))
        {
            matches_expected_ = false;
            if (previous_packet_.packet_index == spec_.packets_per_channel &&
                packet.packet_index == 1U &&
                packet.channel == previous_packet_.channel &&
                packet.prt == static_cast<std::uint16_t>(previous_packet_.prt + 1U))
            {
                channel_batched_ = true;
            }

            std::ostringstream mismatch;
            mismatch << "第 " << checked_packets_ << " 个数据包开始偏离，期望 "
                     << format_point(expected_next_.cpi,
                                     expected_next_.prt,
                                     expected_next_.channel,
                                     expected_next_.packet_index)
                     << "，实际 "
                     << format_point(packet.cpi,
                                     packet.prt,
                                     packet.channel,
                                     packet.packet_index);
            first_mismatch_ = mismatch.str();
        }

        expected_next_ = build_next_expected(packet);
        previous_packet_ = packet;
    }

    void DataOrderTracker::populate_summary(RunSummary &summary) const
    {
        populate_data_order_summary(summary,
                                    checked_packets_,
                                    matches_expected_,
                                    channel_batched_,
                                    first_mismatch_);
    }

} // namespace rxtech
