#include "rxtech/cpi_admission.h"

namespace rxtech
{

    void RecentClosedRing::push(std::uint16_t cpi_id, std::uint64_t seal_tsc, CpiDecision decision) noexcept
    {
        entries_[next_index_] = RecentClosedEntry{cpi_id, seal_tsc, decision, true};
        next_index_ = (next_index_ + 1U) % entries_.size();
    }

    bool RecentClosedRing::contains(std::uint16_t cpi_id) const noexcept
    {
        for (const RecentClosedEntry &entry : entries_)
        {
            if (entry.occupied && entry.cpi_id == cpi_id)
            {
                return true;
            }
        }
        return false;
    }

    AdmissionResult CpiAdmission::judge(const ParsedPacketView &packet,
                                        std::uint16_t active_cpi,
                                        const RecentClosedRing &closed) const noexcept
    {
        if (!packet.valid)
        {
            return AdmissionResult::DROP;
        }
        if (packet.cpi == active_cpi)
        {
            return AdmissionResult::WRITE_ACTIVE;
        }
        if (packet.cpi == static_cast<std::uint16_t>(active_cpi + 1U))
        {
            return AdmissionResult::TRIGGER_CPI_SWITCH;
        }
        if (closed.contains(packet.cpi))
        {
            return AdmissionResult::LATE_TO_CLOSED;
        }
        return AdmissionResult::DROP;
    }

} // namespace rxtech
