#pragma once

#include <cstdint>

namespace rxtech
{
    namespace byte_order
    {

        inline std::uint16_t read_u16_le(const std::uint8_t *data)
        {
            return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) |
                                              (static_cast<std::uint16_t>(data[1]) << 8U));
        }

        inline std::uint32_t read_u32_le(const std::uint8_t *data)
        {
            return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U) |
                   (static_cast<std::uint32_t>(data[2]) << 16U) | (static_cast<std::uint32_t>(data[3]) << 24U);
        }

        inline std::uint16_t read_u16_be(const std::uint8_t *data)
        {
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                              static_cast<std::uint16_t>(data[1]));
        }

        inline std::uint32_t read_u32_be(const std::uint8_t *data)
        {
            return (static_cast<std::uint32_t>(data[0]) << 24U) | (static_cast<std::uint32_t>(data[1]) << 16U) |
                   (static_cast<std::uint32_t>(data[2]) << 8U) | static_cast<std::uint32_t>(data[3]);
        }

    } // namespace byte_order
} // namespace rxtech
