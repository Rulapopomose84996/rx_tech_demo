#define _GNU_SOURCE

#include <errno.h>
#include <net/if.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <bpf/xsk.h>

enum {
    RX_RING_SIZE = 256,
    TX_RING_SIZE = 256,
    FQ_RING_SIZE = 256,
    CQ_RING_SIZE = 256,
    FRAME_SIZE = 2048,
    FRAME_COUNT = 4096,
    UMEM_SIZE = FRAME_SIZE * FRAME_COUNT
};

struct xsk_umem_info {
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    struct xsk_umem* umem;
    void* buffer;
};

struct xsk_socket_info {
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_socket* xsk;
    struct xsk_umem_info* umem;
};

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static int set_memlock_rlimit(void) {
    struct rlimit rlim;
    rlim.rlim_cur = RLIM_INFINITY;
    rlim.rlim_max = RLIM_INFINITY;
    return setrlimit(RLIMIT_MEMLOCK, &rlim);
}

static int configure_umem(struct xsk_umem_info* umem) {
    struct xsk_umem_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.fill_size = FQ_RING_SIZE;
    cfg.comp_size = CQ_RING_SIZE;
    cfg.frame_size = FRAME_SIZE;
    cfg.frame_headroom = 0;
    cfg.flags = 0;

    umem->buffer = mmap(NULL,
                        UMEM_SIZE,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1,
                        0);
    if (umem->buffer == MAP_FAILED) {
        fprintf(stderr, "mmap UMEM failed: %s\n", strerror(errno));
        return -1;
    }

    if (xsk_umem__create(&umem->umem,
                         umem->buffer,
                         UMEM_SIZE,
                         &umem->fq,
                         &umem->cq,
                         &cfg) != 0) {
        fprintf(stderr, "xsk_umem__create failed\n");
        return -1;
    }

    const uint32_t fill_budget = FQ_RING_SIZE;
    uint32_t idx = 0;
    if (xsk_ring_prod__reserve(&umem->fq, fill_budget, &idx) != fill_budget) {
        fprintf(stderr, "reserve fill ring failed\n");
        return -1;
    }

    for (uint32_t i = 0; i < fill_budget; ++i) {
        *xsk_ring_prod__fill_addr(&umem->fq, idx + i) = i * FRAME_SIZE;
    }
    xsk_ring_prod__submit(&umem->fq, fill_budget);
    return 0;
}

static int configure_socket(struct xsk_socket_info* xsk,
                            const char* ifname,
                            uint32_t queue_id,
                            int xsks_map_fd) {
    struct xsk_socket_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.rx_size = RX_RING_SIZE;
    cfg.tx_size = TX_RING_SIZE;
    cfg.libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
    cfg.xdp_flags = 0;
    cfg.bind_flags = XDP_USE_NEED_WAKEUP;

    if (xsk_socket__create(&xsk->xsk,
                           ifname,
                           queue_id,
                           xsk->umem->umem,
                           &xsk->rx,
                           &xsk->tx,
                           &cfg) != 0) {
        fprintf(stderr, "xsk_socket__create failed\n");
        return -1;
    }

    if (xsk_socket__update_xskmap(xsk->xsk, xsks_map_fd) != 0) {
        fprintf(stderr, "xsk_socket__update_xskmap failed\n");
        return -1;
    }

    return 0;
}

static void recycle_completions(struct xsk_umem_info* umem, uint32_t budget) {
    uint32_t idx_cq = 0;
    const uint32_t completed = xsk_ring_cons__peek(&umem->cq, budget, &idx_cq);
    if (completed == 0) {
        return;
    }

    uint32_t idx_fq = 0;
    while (xsk_ring_prod__reserve(&umem->fq, completed, &idx_fq) != completed) {
    }

    for (uint32_t i = 0; i < completed; ++i) {
        *xsk_ring_prod__fill_addr(&umem->fq, idx_fq + i) =
            *xsk_ring_cons__comp_addr(&umem->cq, idx_cq + i);
    }

    xsk_ring_prod__submit(&umem->fq, completed);
    xsk_ring_cons__release(&umem->cq, completed);
}

int main(int argc, char** argv) {
    const char* ifname = argc > 1 ? argv[1] : "enP1s25f3";
    const uint32_t queue_id = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 0U;
    const uint32_t duration_seconds = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 3U;
    const int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        fprintf(stderr, "if_nametoindex(%s) failed\n", ifname);
        return 1;
    }

    if (set_memlock_rlimit() != 0) {
        fprintf(stderr, "setrlimit(RLIMIT_MEMLOCK) failed: %s\n", strerror(errno));
        return 2;
    }

    int xsks_map_fd = -1;
    if (xsk_setup_xdp_prog(ifindex, &xsks_map_fd) != 0) {
        fprintf(stderr, "xsk_setup_xdp_prog failed\n");
        return 3;
    }

    struct xsk_umem_info umem;
    memset(&umem, 0, sizeof(umem));
    if (configure_umem(&umem) != 0) {
        return 4;
    }

    struct xsk_socket_info xsk;
    memset(&xsk, 0, sizeof(xsk));
    xsk.umem = &umem;
    if (configure_socket(&xsk, ifname, queue_id, xsks_map_fd) != 0) {
        return 5;
    }

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = xsk_socket__fd(xsk.xsk);
    pfd.events = POLLIN;

    const uint64_t deadline = monotonic_ns() + ((uint64_t)duration_seconds * 1000000000ULL);
    uint64_t packets = 0;
    uint64_t bytes = 0;
    uint64_t polls = 0;

    while (monotonic_ns() < deadline) {
        ++polls;
        const int rc = poll(&pfd, 1, 200);
        if (rc < 0) {
            fprintf(stderr, "poll failed: %s\n", strerror(errno));
            break;
        }

        uint32_t idx_rx = 0;
        const uint32_t received = xsk_ring_cons__peek(&xsk.rx, RX_RING_SIZE, &idx_rx);
        if (received == 0) {
            recycle_completions(&umem, CQ_RING_SIZE);
            continue;
        }

        for (uint32_t i = 0; i < received; ++i) {
            const struct xdp_desc* desc = xsk_ring_cons__rx_desc(&xsk.rx, idx_rx + i);
            ++packets;
            bytes += desc->len;

            uint32_t idx_fq = 0;
            while (xsk_ring_prod__reserve(&umem.fq, 1, &idx_fq) != 1) {
            }
            *xsk_ring_prod__fill_addr(&umem.fq, idx_fq) = desc->addr;
            xsk_ring_prod__submit(&umem.fq, 1);
        }

        xsk_ring_cons__release(&xsk.rx, received);
        recycle_completions(&umem, received);
    }

    printf("AF_XDP RX PoC success: if=%s queue=%u duration=%u packets=%llu bytes=%llu polls=%llu\n",
           ifname,
           queue_id,
           duration_seconds,
           (unsigned long long)packets,
           (unsigned long long)bytes,
           (unsigned long long)polls);

    xsk_socket__delete(xsk.xsk);
    xsk_umem__delete(umem.umem);
    if (umem.buffer != NULL) {
        munmap(umem.buffer, UMEM_SIZE);
    }
    return 0;
}
