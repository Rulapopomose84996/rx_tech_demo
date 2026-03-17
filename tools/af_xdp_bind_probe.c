#define _GNU_SOURCE

#include <errno.h>
#include <linux/if_xdp.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef AF_XDP
#define AF_XDP 44
#endif

#ifndef SOL_XDP
#define SOL_XDP 283
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

enum {
    RX_RING_SIZE = 64,
    TX_RING_SIZE = 64,
    FQ_RING_SIZE = 64,
    CQ_RING_SIZE = 64,
    FRAME_SIZE = 2048,
    FRAME_COUNT = 64,
    UMEM_SIZE = FRAME_SIZE * FRAME_COUNT
};

static int configure_ring(int fd, int optname, int size) {
    return setsockopt(fd, SOL_XDP, optname, &size, sizeof(size));
}

int main(int argc, char** argv) {
    const char* ifname = argc > 1 ? argv[1] : "enP1s25f3";
    unsigned int queue_id = argc > 2 ? (unsigned int)strtoul(argv[2], NULL, 10) : 0U;
    const int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        fprintf(stderr, "if_nametoindex(%s) failed\n", ifname);
        return 1;
    }

    const int fd = socket(AF_XDP, SOCK_RAW, 0);
    if (fd < 0) {
        fprintf(stderr, "socket(AF_XDP) failed: %s\n", strerror(errno));
        return 2;
    }

    void* umem = mmap(NULL,
                      UMEM_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1,
                      0);
    if (umem == MAP_FAILED) {
        fprintf(stderr, "mmap(umem) failed: %s\n", strerror(errno));
        close(fd);
        return 3;
    }

    struct xdp_umem_reg umem_reg;
    memset(&umem_reg, 0, sizeof(umem_reg));
    umem_reg.addr = (__u64)umem;
    umem_reg.len = UMEM_SIZE;
    umem_reg.chunk_size = FRAME_SIZE;
    umem_reg.headroom = 0;

    if (setsockopt(fd, SOL_XDP, XDP_UMEM_REG, &umem_reg, sizeof(umem_reg)) != 0) {
        fprintf(stderr, "setsockopt(XDP_UMEM_REG) failed: %s\n", strerror(errno));
        munmap(umem, UMEM_SIZE);
        close(fd);
        return 4;
    }

    if (configure_ring(fd, XDP_UMEM_FILL_RING, FQ_RING_SIZE) != 0) {
        fprintf(stderr, "setsockopt(XDP_UMEM_FILL_RING) failed: %s\n", strerror(errno));
        munmap(umem, UMEM_SIZE);
        close(fd);
        return 5;
    }

    if (configure_ring(fd, XDP_UMEM_COMPLETION_RING, CQ_RING_SIZE) != 0) {
        fprintf(stderr, "setsockopt(XDP_UMEM_COMPLETION_RING) failed: %s\n", strerror(errno));
        munmap(umem, UMEM_SIZE);
        close(fd);
        return 6;
    }

    if (configure_ring(fd, XDP_RX_RING, RX_RING_SIZE) != 0) {
        fprintf(stderr, "setsockopt(XDP_RX_RING) failed: %s\n", strerror(errno));
        munmap(umem, UMEM_SIZE);
        close(fd);
        return 7;
    }

    if (configure_ring(fd, XDP_TX_RING, TX_RING_SIZE) != 0) {
        fprintf(stderr, "setsockopt(XDP_TX_RING) failed: %s\n", strerror(errno));
        munmap(umem, UMEM_SIZE);
        close(fd);
        return 8;
    }

    struct xdp_mmap_offsets offsets;
    socklen_t optlen = sizeof(offsets);
    memset(&offsets, 0, sizeof(offsets));
    if (getsockopt(fd, SOL_XDP, XDP_MMAP_OFFSETS, &offsets, &optlen) != 0) {
        fprintf(stderr, "getsockopt(XDP_MMAP_OFFSETS) failed: %s\n", strerror(errno));
        munmap(umem, UMEM_SIZE);
        close(fd);
        return 9;
    }

    const size_t fr_map_sz = offsets.fr.desc + (FQ_RING_SIZE * sizeof(__u64));
    const size_t cr_map_sz = offsets.cr.desc + (CQ_RING_SIZE * sizeof(__u64));
    const size_t rx_map_sz = offsets.rx.desc + (RX_RING_SIZE * sizeof(struct xdp_desc));
    const size_t tx_map_sz = offsets.tx.desc + (TX_RING_SIZE * sizeof(struct xdp_desc));

    void* fr = mmap(NULL, fr_map_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, XDP_UMEM_PGOFF_FILL_RING);
    void* cr = mmap(NULL, cr_map_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, XDP_UMEM_PGOFF_COMPLETION_RING);
    void* rx = mmap(NULL, rx_map_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, XDP_PGOFF_RX_RING);
    void* tx = mmap(NULL, tx_map_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, XDP_PGOFF_TX_RING);

    if (fr == MAP_FAILED || cr == MAP_FAILED || rx == MAP_FAILED || tx == MAP_FAILED) {
        fprintf(stderr, "mmap(rings) failed: %s\n", strerror(errno));
        if (fr != MAP_FAILED) munmap(fr, fr_map_sz);
        if (cr != MAP_FAILED) munmap(cr, cr_map_sz);
        if (rx != MAP_FAILED) munmap(rx, rx_map_sz);
        if (tx != MAP_FAILED) munmap(tx, tx_map_sz);
        munmap(umem, UMEM_SIZE);
        close(fd);
        return 10;
    }

    struct sockaddr_xdp sxdp;
    memset(&sxdp, 0, sizeof(sxdp));
    sxdp.sxdp_family = AF_XDP;
    sxdp.sxdp_ifindex = ifindex;
    sxdp.sxdp_queue_id = queue_id;
    sxdp.sxdp_flags = 0;

    if (bind(fd, (struct sockaddr*)&sxdp, sizeof(sxdp)) != 0) {
        fprintf(stderr,
                "bind(AF_XDP, if=%s index=%d queue=%u) failed: %s\n",
                ifname,
                ifindex,
                queue_id,
                strerror(errno));
        munmap(fr, fr_map_sz);
        munmap(cr, cr_map_sz);
        munmap(rx, rx_map_sz);
        munmap(tx, tx_map_sz);
        munmap(umem, UMEM_SIZE);
        close(fd);
        return 11;
    }

    printf("AF_XDP bind probe success: if=%s index=%d queue=%u\n", ifname, ifindex, queue_id);

    munmap(fr, fr_map_sz);
    munmap(cr, cr_map_sz);
    munmap(rx, rx_map_sz);
    munmap(tx, tx_map_sz);
    munmap(umem, UMEM_SIZE);
    close(fd);
    return 0;
}
