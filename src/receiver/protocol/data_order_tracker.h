#pragma once

#include <cstdint>
#include <string>

#include "rxtech/metrics.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"

namespace rxtech
{

    class DataOrderTracker
    {
      public:
        explicit DataOrderTracker(const ProtocolSpec &spec);

        void observe(const InterpretedPacketView &packet);
        void populate_summary(RunSummary &summary) const;

      private:
        struct Cursor
        {
            std::uint16_t cpi = 0;
            std::uint16_t prt = 0;
            std::uint16_t channel = 0;
            std::uint16_t packet_index = 0;
        };

        Cursor build_next_expected(const InterpretedPacketView &packet) const;
        static bool matches_expected(const InterpretedPacketView &packet, const Cursor &expected);
        static std::string format_point(std::uint16_t cpi, std::uint16_t prt, std::uint16_t channel,
                                        std::uint16_t packet_index);

        ProtocolSpec spec_{};
        std::uint64_t checked_packets_ = 0;
        bool initialized_ = false;
        bool matches_expected_ = true;
        bool channel_batched_ = false;
        std::string first_mismatch_;
        Cursor expected_next_{};
        InterpretedPacketView previous_packet_{};
    };

} // namespace rxtech
