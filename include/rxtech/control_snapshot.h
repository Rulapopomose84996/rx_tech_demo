#pragma once

#include <cstdint>

namespace rxtech
{

    enum class BindSource : std::uint8_t
    {
        fixed,
        provisional,
        control
    };

    struct ControlSnapshot
    {
        std::uint16_t cpi_id = 0;
        std::uint16_t n_prt = 0;
        std::uint16_t channel_count = 0;
        std::uint16_t packets_per_channel = 0;
        std::uint64_t timeout_ns = 0;
        bool valid = false;
        BindSource bind_source = BindSource::fixed;
        bool conflict = false;
    };

} // namespace rxtech
