#include <errno.h>
#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* ifname = argc > 1 ? argv[1] : "enP1s25f3";
    unsigned int queue_id = argc > 2 ? (unsigned int)strtoul(argv[2], NULL, 10) : 0U;
    int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        fprintf(stderr, "if_nametoindex(%s) failed\n", ifname);
        return 1;
    }

    int fd = socket(AF_XDP, SOCK_RAW, 0);
    if (fd < 0) {
        fprintf(stderr, "socket(AF_XDP) failed: %s\n", strerror(errno));
        return 2;
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
        close(fd);
        return 3;
    }

    printf("AF_XDP bind probe success: if=%s index=%d queue=%u\n", ifname, ifindex, queue_id);
    close(fd);
    return 0;
}
