#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rxtech {

struct PacketDesc {
    std::uint8_t* data = nullptr;
    std::uint32_t len = 0;
    std::uint64_t ts_ns = 0;
    std::uint32_t port_id = 0;
    std::uint32_t queue_id = 0;
    std::uint32_t face_id = 0;
    std::uintptr_t cookie = 0;
};

struct RxBurst {
    std::vector<PacketDesc> packets;
};

}  // namespace rxtech
