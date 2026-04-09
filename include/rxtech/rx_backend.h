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
        std::uint64_t rx_polls = 0;
        std::uint64_t empty_polls = 0;
        std::uint64_t arp_request_packets = 0;
        std::uint64_t arp_reply_packets = 0;
        std::uint32_t queue_id = 0;
        std::uint64_t umem_size = 0;
        std::uint32_t frame_size = 0;
        std::uint32_t fill_ring_size = 0;
        std::uint32_t completion_ring_size = 0;
    };

    class IRxBackend
    {
    public:
        virtual ~IRxBackend() = default;

        virtual std::string name() const = 0;
        virtual BackendInitResult init(const RxConfig &config) = 0;
        virtual bool recv_burst(UdpDatagramBurst &burst, std::uint32_t max_burst) = 0;
        virtual void release_burst(UdpDatagramBurst &burst) = 0;
        virtual BackendStats stats() const = 0;
        virtual void shutdown() = 0;
    };

    using BackendPtr = std::unique_ptr<IRxBackend>;

} // namespace rxtech
