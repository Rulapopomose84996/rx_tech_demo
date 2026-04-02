#include <cassert>

#include "rxtech/rx_config.h"

int main() {
    rxtech::RxConfig base = rxtech::load_default_config();
    rxtech::RxConfig overrides = rxtech::load_default_config();
    overrides.backend_name.clear();
    overrides.output_dir = "results/merged";
    overrides.queue_id = 3U;
    overrides.max_burst = 32U;
    overrides.duration_seconds = 7U;
    overrides.cpu_cores = {16, 17};
    overrides.xdp_rx_ring_size = 1024U;
    overrides.xdp_fill_ring_size = 2048U;
    overrides.xdp_frame_count = 8192U;
    overrides.xdp_poll_timeout_ms = 3U;
    overrides.allowed_source_ipv4 = "172.20.11.222";
    overrides.allowed_dest_port = 9999U;

    rxtech::merge_config(base, overrides);
    assert(base.output_dir == "results/merged");
    assert(base.queue_id == 3U);
    assert(base.max_burst == 32U);
    assert(base.duration_seconds == 7U);
    assert(base.cpu_cores.size() == 2U);
    assert(base.cpu_cores[1] == 17);
    assert(base.xdp_rx_ring_size == 1024U);
    assert(base.xdp_fill_ring_size == 2048U);
    assert(base.xdp_frame_count == 8192U);
    assert(base.xdp_poll_timeout_ms == 3U);
    assert(base.allowed_source_ipv4 == "172.20.11.222");
    assert(base.allowed_dest_port == 9999U);
    return 0;
}
