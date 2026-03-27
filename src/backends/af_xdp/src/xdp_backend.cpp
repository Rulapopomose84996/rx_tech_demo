#include "rxtech/xdp_backend.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <vector>

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

#ifdef __linux__
#include <net/if.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/xsk.h>

#ifndef SOL_XDP
#define SOL_XDP 283
#endif
#endif

namespace rxtech {

namespace {

BackendInitResult make_xdp_result(bool available, const std::string& reason) {
    BackendInitResult result;
    result.available = available;
    result.reason = reason;
    return result;
}

BackendInitResult make_xdp_errno_result(const std::string& reason) {
    return make_xdp_result(true, reason + ": " + std::strerror(errno));
}

std::uint32_t clamp_to_power_of_two(std::uint32_t value, std::uint32_t minimum) {
    std::uint32_t clamped = std::max(value, minimum);
    if ((clamped & (clamped - 1U)) == 0U) {
        return clamped;
    }

    std::uint32_t rounded = 1U;
    while (rounded < clamped && rounded < (1U << 30U)) {
        rounded <<= 1U;
    }
    return rounded;
}

}  // namespace

struct XdpBackend::Impl {
#ifdef __linux__
    struct UmemInfo {
        xsk_ring_prod fq;
        xsk_ring_cons cq;
        xsk_umem* umem = nullptr;
        void* buffer = nullptr;
    };

    struct SocketInfo {
        xsk_ring_cons rx;
        xsk_ring_prod tx;
        xsk_socket* xsk = nullptr;
        bool zerocopy = false;
    };

    static constexpr std::uint32_t kMinRingSize = 64U;
    static constexpr std::uint32_t kMinFrameSize = 2048U;
    static constexpr std::uint32_t kMinFrameCount = 1024U;

    std::string ifname;
    std::string bind_mode = "auto";
    std::uint32_t queue_id = 0;
    std::uint32_t rx_ring_size = 1024U;
    std::uint32_t tx_ring_size = 256U;
    std::uint32_t fill_ring_size = 2048U;
    std::uint32_t completion_ring_size = 2048U;
    std::uint32_t frame_size = 2048U;
    std::uint32_t frame_count = 4096U;
    std::uint32_t poll_timeout_ms = 0U;
    std::uint64_t umem_size = static_cast<std::uint64_t>(frame_size) * frame_count;
    int ifindex = 0;
    int xsks_map_fd = -1;
    UmemInfo umem;
    SocketInfo socket;
    pollfd pfd{};
    std::vector<std::uint64_t> pending_fill_addrs;

    static bool set_memlock_rlimit() {
        rlimit rlim{};
        rlim.rlim_cur = RLIM_INFINITY;
        rlim.rlim_max = RLIM_INFINITY;
        return setrlimit(RLIMIT_MEMLOCK, &rlim) == 0;
    }

    void apply_config(const RxConfig& config) {
        rx_ring_size = clamp_to_power_of_two(config.xdp_rx_ring_size, kMinRingSize);
        tx_ring_size = clamp_to_power_of_two(config.xdp_tx_ring_size, kMinRingSize);
        fill_ring_size = clamp_to_power_of_two(config.xdp_fill_ring_size, kMinRingSize);
        completion_ring_size = clamp_to_power_of_two(config.xdp_completion_ring_size, kMinRingSize);
        frame_size = std::max(config.xdp_frame_size, kMinFrameSize);
        frame_count = clamp_to_power_of_two(config.xdp_frame_count, kMinFrameCount);
        frame_count = std::max(frame_count, fill_ring_size);
        poll_timeout_ms = config.xdp_poll_timeout_ms;
        umem_size = static_cast<std::uint64_t>(frame_size) * static_cast<std::uint64_t>(frame_count);
    }

    std::uint32_t reserve_fill_slots(std::uint32_t wanted, std::uint32_t* idx) {
        std::uint32_t chunk = wanted;
        while (chunk > 0U) {
            if (xsk_ring_prod__reserve(&umem.fq, chunk, idx) == chunk) {
                return chunk;
            }
            chunk >>= 1U;
        }
        return 0U;
    }

    void flush_pending_fill() {
        std::size_t offset = 0U;
        while (offset < pending_fill_addrs.size()) {
            std::uint32_t idx = 0;
            const std::uint32_t remaining =
                static_cast<std::uint32_t>(pending_fill_addrs.size() - offset);
            const std::uint32_t reserved = reserve_fill_slots(remaining, &idx);
            if (reserved == 0U) {
                break;
            }

            for (std::uint32_t i = 0; i < reserved; ++i) {
                *xsk_ring_prod__fill_addr(&umem.fq, idx + i) = pending_fill_addrs[offset + i];
            }
            xsk_ring_prod__submit(&umem.fq, reserved);
            offset += reserved;
        }

        if (offset > 0U) {
            pending_fill_addrs.erase(pending_fill_addrs.begin(),
                                     pending_fill_addrs.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }

    bool configure_umem() {
        xsk_umem_config cfg{};
        cfg.fill_size = fill_ring_size;
        cfg.comp_size = completion_ring_size;
        cfg.frame_size = frame_size;
        cfg.frame_headroom = 0;
        cfg.flags = 0;

        umem.buffer = mmap(NULL,
                           umem_size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1,
                           0);
        if (umem.buffer == MAP_FAILED) {
            umem.buffer = nullptr;
            return false;
        }

        if (xsk_umem__create(&umem.umem,
                             umem.buffer,
                             umem_size,
                             &umem.fq,
                             &umem.cq,
                             &cfg) != 0) {
            return false;
        }

        pending_fill_addrs.clear();
        pending_fill_addrs.reserve(frame_count);
        for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
            pending_fill_addrs.push_back(static_cast<std::uint64_t>(frame) * frame_size);
        }
        flush_pending_fill();
        if (pending_fill_addrs.size() == static_cast<std::size_t>(frame_count)) {
            return false;
        }
        return true;
    }

    bool configure_socket() {
        xsk_socket_config cfg{};
        cfg.rx_size = rx_ring_size;
        cfg.tx_size = tx_ring_size;
        cfg.libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
        cfg.xdp_flags = 0;
        cfg.bind_flags = XDP_USE_NEED_WAKEUP;

        if (bind_mode == "copy") {
            cfg.bind_flags |= XDP_COPY;
        } else if (bind_mode == "zerocopy") {
            cfg.bind_flags |= XDP_ZEROCOPY;
        }

        if (xsk_socket__create(&socket.xsk,
                               ifname.c_str(),
                               queue_id,
                               umem.umem,
                               &socket.rx,
                               &socket.tx,
                               &cfg) != 0) {
            return false;
        }

        if (xsk_socket__update_xskmap(socket.xsk, xsks_map_fd) != 0) {
            return false;
        }

        xdp_options opts{};
        socklen_t opts_len = sizeof(opts);
        if (getsockopt(xsk_socket__fd(socket.xsk), SOL_XDP, XDP_OPTIONS, &opts, &opts_len) == 0) {
            socket.zerocopy = (opts.flags & XDP_OPTIONS_ZEROCOPY) != 0;
        } else {
            socket.zerocopy = false;
        }

        pfd.fd = xsk_socket__fd(socket.xsk);
        pfd.events = POLLIN;
        return pfd.fd >= 0;
    }

    void cleanup() {
        if (socket.xsk != nullptr) {
            xsk_socket__delete(socket.xsk);
            socket.xsk = nullptr;
        }
        if (xsks_map_fd >= 0) {
            close(xsks_map_fd);
            xsks_map_fd = -1;
        }
        if (umem.umem != nullptr) {
            xsk_umem__delete(umem.umem);
            umem.umem = nullptr;
        }
        if (umem.buffer != nullptr) {
            munmap(umem.buffer, umem_size);
            umem.buffer = nullptr;
        }
        pending_fill_addrs.clear();
        if (ifindex > 0) {
            (void)bpf_set_link_xdp_fd(ifindex, -1, 0);
        }
    }
#endif
};

XdpBackend::XdpBackend() : impl_(new Impl()) {
}

XdpBackend::~XdpBackend() {
    shutdown();
}

std::string XdpBackend::name() const {
    return "af_xdp";
}

BackendInitResult XdpBackend::init(const RxConfig& config) {
    stats_ = {};
#ifdef __linux__
    if (!Impl::set_memlock_rlimit()) {
        return make_xdp_errno_result("setrlimit(RLIMIT_MEMLOCK) failed");
    }

    impl_->apply_config(config);
    impl_->ifname = config.interface_name;
    impl_->bind_mode = config.xdp_bind_mode;
    impl_->queue_id = config.queue_id;
    impl_->ifindex = if_nametoindex(config.interface_name.c_str());
    if (impl_->ifindex == 0) {
        return make_xdp_errno_result("if_nametoindex() failed for " + config.interface_name);
    }

    if (xsk_setup_xdp_prog(impl_->ifindex, &impl_->xsks_map_fd) != 0) {
        return make_xdp_errno_result("xsk_setup_xdp_prog() failed");
    }
    if (!impl_->configure_umem()) {
        impl_->cleanup();
        return make_xdp_errno_result("configure_umem() failed");
    }
    if (!impl_->configure_socket()) {
        impl_->cleanup();
        return make_xdp_errno_result("configure_socket() failed");
    }

    std::uint32_t prog_id = 0;
    if (bpf_get_link_xdp_id(impl_->ifindex, &prog_id, 0) == 0) {
        stats_.xdp_prog_id = prog_id;
    }
    stats_.queue_id = impl_->queue_id;
    stats_.xdp_attach_mode = "driver";
    stats_.xsk_mode = impl_->socket.zerocopy ? "zerocopy" : "copy";
    stats_.xsk_bind_flags = XDP_USE_NEED_WAKEUP;
    if (impl_->bind_mode == "copy") {
        stats_.xsk_bind_flags |= XDP_COPY;
    } else if (impl_->bind_mode == "zerocopy") {
        stats_.xsk_bind_flags |= XDP_ZEROCOPY;
    }
    stats_.umem_size = impl_->umem_size;
    stats_.frame_size = impl_->frame_size;
    stats_.fill_ring_size = impl_->fill_ring_size;
    stats_.completion_ring_size = impl_->completion_ring_size;
    BackendInitResult result;
    result.ok = true;
    return result;
#else
    (void)config;
    return make_xdp_result(false, "AF_XDP backend requires Linux, libbpf and XDP socket support");
#endif
}

bool XdpBackend::recv_burst(RxBurst& burst, std::uint32_t max_burst) {
    burst.packets.clear();
#ifdef __linux__
    if (impl_ == nullptr || impl_->socket.xsk == nullptr) {
        return false;
    }

    ++stats_.rx_polls;
    std::uint32_t idx_rx = 0;
    const std::uint32_t budget = std::min<std::uint32_t>(max_burst, impl_->rx_ring_size);
    std::uint32_t received = xsk_ring_cons__peek(&impl_->socket.rx, budget, &idx_rx);
    if (received == 0U && impl_->poll_timeout_ms > 0U) {
        const int poll_rc = poll(&impl_->pfd, 1, static_cast<int>(impl_->poll_timeout_ms));
        if (poll_rc < 0) {
            if (errno == EINTR) {
                return true;
            }
            ++stats_.rx_errors;
            return false;
        }
        if (poll_rc > 0) {
            received = xsk_ring_cons__peek(&impl_->socket.rx, budget, &idx_rx);
        }
    }

    if (received == 0U) {
        ++stats_.empty_polls;
        return true;
    }

    burst.packets.reserve(received);
    const std::uint64_t burst_ts_ns = steady_clock_now_ns();
    for (std::uint32_t i = 0; i < received; ++i) {
        const xdp_desc* desc = xsk_ring_cons__rx_desc(&impl_->socket.rx, idx_rx + i);
        PacketDesc packet;
        packet.data = static_cast<std::uint8_t*>(xsk_umem__get_data(impl_->umem.buffer, desc->addr));
        packet.len = desc->len;
        packet.ts_ns = burst_ts_ns;
        packet.queue_id = impl_->queue_id;
        packet.cookie = static_cast<std::uintptr_t>(desc->addr);
        burst.packets.push_back(packet);
        ++stats_.rx_packets;
        stats_.rx_bytes += desc->len;
    }
    xsk_ring_cons__release(&impl_->socket.rx, received);
    return true;
#else
    (void)max_burst;
    return false;
#endif
}

void XdpBackend::release_burst(RxBurst& burst) {
#ifdef __linux__
    if (impl_ != nullptr && impl_->socket.xsk != nullptr && !burst.packets.empty()) {
        impl_->pending_fill_addrs.reserve(impl_->pending_fill_addrs.size() + burst.packets.size());
        for (const PacketDesc& packet : burst.packets) {
            impl_->pending_fill_addrs.push_back(static_cast<std::uint64_t>(packet.cookie));
        }
        impl_->flush_pending_fill();
    }
#endif
    burst.packets.clear();
}

BackendStats XdpBackend::stats() const {
    return stats_;
}

void XdpBackend::shutdown() {
#ifdef __linux__
    if (impl_ != nullptr) {
        impl_->cleanup();
    }
#endif
}

}  // namespace rxtech
