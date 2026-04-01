#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct bpf_map_def SEC("maps") xsks_map = {
    .type = BPF_MAP_TYPE_XSKMAP,
    .key_size = sizeof(__u32),
    .value_size = sizeof(__u32),
    .max_entries = 64,
};

SEC("xdp")
int rxtech_xdp_redirect(struct xdp_md* ctx) {
    return bpf_redirect_map(&xsks_map, ctx->rx_queue_index, 0);
}

char LICENSE[] SEC("license") = "GPL";
