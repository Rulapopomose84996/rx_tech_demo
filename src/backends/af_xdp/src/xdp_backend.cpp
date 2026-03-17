#include "rxtech/xdp_backend.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

#ifdef __linux__
#define _GNU_SOURCE
#include <net/if.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/xsk.h>
#endif

namespace rxtech {

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
    };

    static constexpr std::uint32_t kRxRingSize = 256U;
    static constexpr std::uint32_t kTxRingSize = 256U;
    static constexpr std::uint32_t kFillRingSize = 256U;
    static constexpr std::uint32_t kCompletionRingSize = 256U;
    static constexpr std::uint32_t kFrameSize = 2048U;
    static constexpr std::uint32_t kFrameCount = 4096U;
    static constexpr std::uint64_t kUmemSize =
        static_cast<std::uint64_t>(kFrameSize) * static_cast<std::uint64_t>(kFrameCount);

    std::string ifname;
    std::uint32_t queue_id = 0;
    int ifindex = 0;
    int xsks_map_fd = -1;
    UmemInfo umem;
    SocketInfo socket;
    pollfd pfd{};

    static bool set_memlock_rlimit() {
        rlimit rlim{};
        rlim.rlim_cur = RLIM_INFINITY;
        rlim.rlim_max = RLIM_INFINITY;
        return setrlimit(RLIMIT_MEMLOCK, &rlim) == 0;
    }

    bool configure_umem() {
        xsk_umem_config cfg{};
        cfg.fill_size = kFillRingSize;
        cfg.comp_size = kCompletionRingSize;
        cfg.frame_size = kFrameSize;
        cfg.frame_headroom = 0;
        cfg.flags = 0;

        umem.buffer = mmap(NULL,
                           kUmemSize,
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
                             kUmemSize,
                             &umem.fq,
                             &umem.cq,
                             &cfg) != 0) {
            return false;
        }

        std::uint32_t idx = 0;
        if (xsk_ring_prod__reserve(&umem.fq, kFillRingSize, &idx) != kFillRingSize) {
            return false;
        }

        for (std::uint32_t frame = 0; frame < kFillRingSize; ++frame) {
            *xsk_ring_prod__fill_addr(&umem.fq, idx + frame) =
                static_cast<std::uint64_t>(frame) * kFrameSize;
        }
        xsk_ring_prod__submit(&umem.fq, kFillRingSize);
        return true;
    }

    bool configure_socket() {
        xsk_socket_config cfg{};
        cfg.rx_size = kRxRingSize;
        cfg.tx_size = kTxRingSize;
        cfg.libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
        cfg.xdp_flags = 0;
        cfg.bind_flags = XDP_USE_NEED_WAKEUP;

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

        pfd.fd = xsk_socket__fd(socket.xsk);
        pfd.events = POLLIN;
        return pfd.fd >= 0;
    }

    void recycle_completion(std::uint32_t budget) {
        std::uint32_t idx_cq = 0;
        const std::uint32_t completed = xsk_ring_cons__peek(&umem.cq, budget, &idx_cq);
        if (completed == 0U) {
            return;
        }

        std::uint32_t idx_fq = 0;
        while (xsk_ring_prod__reserve(&umem.fq, completed, &idx_fq) != completed) {
        }

        for (std::uint32_t i = 0; i < completed; ++i) {
            *xsk_ring_prod__fill_addr(&umem.fq, idx_fq + i) =
                *xsk_ring_cons__comp_addr(&umem.cq, idx_cq + i);
        }

        xsk_ring_prod__submit(&umem.fq, completed);
        xsk_ring_cons__release(&umem.cq, completed);
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
            munmap(umem.buffer, kUmemSize);
            umem.buffer = nullptr;
        }
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

bool XdpBackend::init(const RxConfig& config) {
    stats_ = {};
#ifdef __linux__
    if (!Impl::set_memlock_rlimit()) {
        return false;
    }

    impl_->ifname = config.interface_name;
    impl_->queue_id = config.queue_id;
    impl_->ifindex = if_nametoindex(config.interface_name.c_str());
    if (impl_->ifindex == 0) {
        return false;
    }

    if (xsk_setup_xdp_prog(impl_->ifindex, &impl_->xsks_map_fd) != 0) {
        return false;
    }
    if (!impl_->configure_umem()) {
        impl_->cleanup();
        return false;
    }
    if (!impl_->configure_socket()) {
        impl_->cleanup();
        return false;
    }

    std::uint32_t prog_id = 0;
    if (bpf_get_link_xdp_id(impl_->ifindex, &prog_id, 0) == 0) {
        stats_.xdp_prog_id = prog_id;
    }
    stats_.queue_id = impl_->queue_id;
    stats_.xdp_attach_mode = "auto";
    stats_.xsk_bind_flags = XDP_USE_NEED_WAKEUP;
    stats_.umem_size = Impl::kUmemSize;
    stats_.frame_size = Impl::kFrameSize;
    stats_.fill_ring_size = Impl::kFillRingSize;
    stats_.completion_ring_size = Impl::kCompletionRingSize;
    return true;
#else
    (void)config;
    return false;
#endif
}

bool XdpBackend::recv_burst(RxBurst& burst, std::uint32_t max_burst) {
    burst.packets.clear();
#ifdef __linux__
    if (impl_ == nullptr || impl_->socket.xsk == nullptr) {
        return false;
    }

    ++stats_.rx_polls;
    const int poll_rc = poll(&impl_->pfd, 1, 100);
    if (poll_rc < 0) {
        ++stats_.rx_errors;
        return false;
    }

    std::uint32_t idx_rx = 0;
    const std::uint32_t budget = std::min<std::uint32_t>(max_burst, Impl::kRxRingSize);
    const std::uint32_t received = xsk_ring_cons__peek(&impl_->socket.rx, budget, &idx_rx);
    if (received == 0U) {
        ++stats_.empty_polls;
        impl_->recycle_completion(Impl::kCompletionRingSize);
        return true;
    }

    burst.packets.reserve(received);
    for (std::uint32_t i = 0; i < received; ++i) {
        const xdp_desc* desc = xsk_ring_cons__rx_desc(&impl_->socket.rx, idx_rx + i);
        PacketDesc packet;
        packet.data = static_cast<std::uint8_t*>(xsk_umem__get_data(impl_->umem.buffer, desc->addr));
        packet.len = desc->len;
        packet.ts_ns = steady_clock_now_ns();
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
        std::uint32_t idx_fq = 0;
        const std::uint32_t count = static_cast<std::uint32_t>(burst.packets.size());
        while (xsk_ring_prod__reserve(&impl_->umem.fq, count, &idx_fq) != count) {
            impl_->recycle_completion(count);
        }

        for (std::uint32_t i = 0; i < count; ++i) {
            *xsk_ring_prod__fill_addr(&impl_->umem.fq, idx_fq + i) =
                static_cast<std::uint64_t>(burst.packets[i].cookie);
        }
        xsk_ring_prod__submit(&impl_->umem.fq, count);
        impl_->recycle_completion(count);
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
