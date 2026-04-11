#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rxtech/udp_datagram.h"

namespace rxtech
{

    struct PacketDesc
    {
        std::uint8_t *data = nullptr; ///< 数据包数据指针
        std::uint32_t len = 0;        ///< 数据包长度（字节）
        std::uint64_t ts_ns = 0;      ///< 时间戳（纳秒）
        std::uint32_t port_id = 0;    ///< 接收端口标识符
        std::uint32_t queue_id = 0;   ///< 队列标识符
        std::uint32_t face_id = 0;    ///< 面（Face）标识符
        std::uintptr_t cookie = 0;    ///< 用户自定义cookie值
    };

    struct RxBurst
    {
        std::vector<PacketDesc> packets;
    };

} // namespace rxtech
