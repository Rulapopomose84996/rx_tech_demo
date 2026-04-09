#pragma once

#include <cstdint>
#include <vector>

namespace rxtech
{

    enum class BackendKind : std::uint8_t
    {
        unknown = 0,
        dpdk,
        socket,
        file_replay
    };

    struct UdpDatagramDesc
    {
        const std::uint8_t *payload_data = nullptr;
        std::uint32_t payload_len = 0;
        std::uint32_t src_ipv4_be = 0;
        std::uint32_t dst_ipv4_be = 0;
        std::uint16_t src_port = 0;
        std::uint16_t dst_port = 0;
        std::uint64_t ts_ns = 0;
        std::uint32_t queue_id = 0;
        std::uintptr_t cookie = 0;
        BackendKind backend_kind = BackendKind::unknown;
        bool truncated = false;
    };

    struct UdpDatagramBurst
    {
        std::vector<UdpDatagramDesc> datagrams;
    };

} // namespace rxtech
