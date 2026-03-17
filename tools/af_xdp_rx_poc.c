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
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/xsk.h>

#ifndef SOL_XDP
#define SOL_XDP 283
#endif

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
    int fd;
    uint16_t bind_flags;
    bool zerocopy;
};

struct xdp_prog_info {
    struct bpf_object* obj;
    int prog_fd;
    int xsks_map_fd;
    bool attached;
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

static int load_and_attach_xdp(struct xdp_prog_info* prog, int ifindex, const char* obj_path) {
    memset(prog, 0, sizeof(*prog));
    prog->prog_fd = -1;
    prog->xsks_map_fd = -1;

    prog->obj = bpf_object__open_file(obj_path, NULL);
    if (prog->obj == NULL) {
        fprintf(stderr, "bpf_object__open_file failed: %s\n", obj_path);
        return -1;
    }

    if (bpf_object__load(prog->obj) != 0) {
        fprintf(stderr, "bpf_object__load failed\n");
        return -1;
    }

    struct bpf_program* program = bpf_object__find_program_by_name(prog->obj, "rxtech_xdp_redirect");
    if (program == NULL) {
        fprintf(stderr, "find xdp program failed\n");
        return -1;
    }
    prog->prog_fd = bpf_program__fd(program);
    if (prog->prog_fd < 0) {
        fprintf(stderr, "bpf_program__fd failed\n");
        return -1;
    }

    prog->xsks_map_fd = bpf_object__find_map_fd_by_name(prog->obj, "xsks_map");
    if (prog->xsks_map_fd < 0) {
        fprintf(stderr, "find xsks_map fd failed\n");
        return -1;
    }

    if (bpf_set_link_xdp_fd(ifindex, prog->prog_fd, 0) != 0) {
        fprintf(stderr, "bpf_set_link_xdp_fd attach failed: %s\n", strerror(errno));
        return -1;
    }
    prog->attached = true;
    return 0;
}

static void detach_xdp(struct xdp_prog_info* prog, int ifindex) {
    if (prog->attached) {
        (void)bpf_set_link_xdp_fd(ifindex, -1, 0);
        prog->attached = false;
    }
    if (prog->obj != NULL) {
        bpf_object__close(prog->obj);
        prog->obj = NULL;
    }
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

    xsk->fd = xsk_socket__fd(xsk->xsk);
    xsk->bind_flags = cfg.bind_flags;

    struct xdp_options opts;
    socklen_t opts_len = sizeof(opts);
    memset(&opts, 0, sizeof(opts));
    if (getsockopt(xsk->fd, SOL_XDP, XDP_OPTIONS, &opts, &opts_len) == 0) {
        xsk->zerocopy = (opts.flags & XDP_OPTIONS_ZEROCOPY) != 0;
    } else {
        xsk->zerocopy = false;
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
    const char* obj_path = argc > 4 ? argv[4] : "build_af_xdp_probe/xdp_redirect_kern.bpf.o";
    const int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        fprintf(stderr, "if_nametoindex(%s) failed\n", ifname);
        return 1;
    }

    if (set_memlock_rlimit() != 0) {
        fprintf(stderr, "setrlimit(RLIMIT_MEMLOCK) failed: %s\n", strerror(errno));
        return 2;
    }

    struct xdp_prog_info xdp_prog;
    if (load_and_attach_xdp(&xdp_prog, ifindex, obj_path) != 0) {
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
    if (configure_socket(&xsk, ifname, queue_id, xdp_prog.xsks_map_fd) != 0) {
        detach_xdp(&xdp_prog, ifindex);
        return 5;
    }

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = xsk.fd;
    pfd.events = POLLIN;

    const uint64_t deadline = monotonic_ns() + ((uint64_t)duration_seconds * 1000000000ULL);
    uint64_t packets = 0;
    uint64_t bytes = 0;
    uint64_t polls = 0;
    uint64_t empty_polls = 0;

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
            ++empty_polls;
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

    const double seconds = duration_seconds > 0 ? (double)duration_seconds : 1.0;
    const double pps = (double)packets / seconds;
    const double bps = ((double)bytes * 8.0) / seconds;
    const double empty_poll_ratio = polls > 0 ? ((double)empty_polls / (double)polls) : 0.0;

    printf("AF_XDP RX PoC success: if=%s queue=%u duration=%u packets=%llu bytes=%llu polls=%llu pps=%.2f bps=%.2f xdp_attach_mode=driver bind_flags=%u xsk_mode=%s empty_poll_ratio=%.4f\n",
           ifname,
           queue_id,
           duration_seconds,
           (unsigned long long)packets,
           (unsigned long long)bytes,
           (unsigned long long)polls,
           pps,
           bps,
           (unsigned int)xsk.bind_flags,
           xsk.zerocopy ? "zerocopy" : "copy",
           empty_poll_ratio);

    xsk_socket__delete(xsk.xsk);
    xsk_umem__delete(umem.umem);
    if (umem.buffer != NULL) {
        munmap(umem.buffer, UMEM_SIZE);
    }
    detach_xdp(&xdp_prog, ifindex);
    return 0;
}
