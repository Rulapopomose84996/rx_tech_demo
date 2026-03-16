#pragma once

#include <chrono>
#include <cstdint>

namespace rxtech {

inline std::uint64_t steady_clock_now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

}  // namespace rxtech
