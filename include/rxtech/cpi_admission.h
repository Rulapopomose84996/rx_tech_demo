#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rxtech/cpi_context.h"
#include "rxtech/sample_packet_parser.h"

namespace rxtech
{

    enum class AdmissionResult
    {
        WRITE_ACTIVE,
        TRIGGER_CPI_SWITCH,
        LATE_TO_CLOSED,
        DROP,
    };

    struct RecentClosedEntry
    {
        std::uint16_t cpi_id = 0;
        std::uint64_t seal_tsc = 0;
        CpiDecision decision = CpiDecision::DISCARD_INVALID;
        bool occupied = false;
    };

    class RecentClosedRing
    {
    public:
        void push(std::uint16_t cpi_id, std::uint64_t seal_tsc, CpiDecision decision) noexcept;
        bool contains(std::uint16_t cpi_id) const noexcept;

    private:
        static constexpr std::size_t kDepth = 8U;
        std::array<RecentClosedEntry, kDepth> entries_{};
        std::size_t next_index_ = 0U;
    };

    class CpiAdmission
    {
    public:
        AdmissionResult judge(const ParsedPacketView &packet,
                              std::uint16_t active_cpi,
                              const RecentClosedRing &closed) const noexcept;
    };

} // namespace rxtech
