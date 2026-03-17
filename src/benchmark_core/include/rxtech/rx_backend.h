#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "rxtech/packet_desc.h"

namespace rxtech {

struct RxConfig;

struct BackendStats {
    std::uint64_t rx_packets = 0;
    std::uint64_t rx_bytes = 0;
    std::uint64_t rx_errors = 0;
    std::uint64_t backend_drops = 0;
    std::uint64_t rx_polls = 0;
    std::uint64_t empty_polls = 0;
    std::uint32_t queue_id = 0;
    std::uint32_t xdp_prog_id = 0;
    std::uint32_t xsk_bind_flags = 0;
    std::uint64_t umem_size = 0;
    std::uint32_t frame_size = 0;
    std::uint32_t fill_ring_size = 0;
    std::uint32_t completion_ring_size = 0;
    std::string xdp_attach_mode = "pending";
};

class IRxBackend {
public:
    virtual ~IRxBackend() = default;

    virtual std::string name() const = 0;
    virtual bool init(const RxConfig& config) = 0;
    virtual bool recv_burst(RxBurst& burst, std::uint32_t max_burst) = 0;
    virtual void release_burst(RxBurst& burst) = 0;
    virtual BackendStats stats() const = 0;
    virtual void shutdown() = 0;
};

using BackendPtr = std::unique_ptr<IRxBackend>;

}  // namespace rxtech
