#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "rxtech/udp_datagram.h"

namespace rxtech
{

    struct RxConfig;

    struct BackendInitResult
    {
        bool ok = false;
        bool available = true;
        std::string reason;
    };

    struct BackendStats
    {
        std::uint64_t rx_packets = 0;
        std::uint64_t rx_bytes = 0;
        std::uint64_t rx_errors = 0;
        std::uint64_t backend_drops = 0;
        std::uint64_t kernel_drop_count = 0;
        std::uint64_t rx_polls = 0;
        std::uint64_t empty_polls = 0;
        std::uint64_t receive_batches = 0;
        std::uint64_t arp_request_packets = 0;
        std::uint64_t arp_reply_packets = 0;
        std::uint32_t queue_id = 0;
        std::uint32_t frame_size = 0;
        std::uint32_t max_burst_size = 0;
    };

    class IRxBackend
    {
      public:
        virtual ~IRxBackend() = default;

        virtual std::string name() const = 0;
        virtual BackendInitResult init(const RxConfig &config) = 0;
        virtual bool recv_burst(UdpDatagramBurst &burst, std::uint32_t max_burst) = 0;

        /**
         * @brief 释放一次 recv_burst() 交付的 burst 生命周期资源。
         *
         * 调用后，burst 中各个 UdpDatagramDesc 持有的 payload/raw frame 指针都不再可用。
         * 调用方必须保证在消费完本次 burst 里需要的数据后立即释放，且同一个 burst 只释放一次。
         *
         * 后端语义：
         * - DPDK：归还底层 mbuf，成本与 burst 中有效报文数量相关。
         * - Socket/FileReplay：重置本次 burst 容器，不做逐包动态释放。
         */
        virtual void release_burst(UdpDatagramBurst &burst) = 0;
        virtual BackendStats stats() const = 0;
        virtual void shutdown() = 0;
    };

    using BackendPtr = std::unique_ptr<IRxBackend>;

} // namespace rxtech
