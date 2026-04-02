#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <fstream>

#include "rxtech/rx_config.h"

int main()
{
    const char *path = "test_runtime_legacy_xdp_config.conf";
    {
        std::ofstream out(path, std::ios::trunc);
        out << "backend: dpdk\n";
        out << "xdp_bind_mode: copy\n";
        out << "xdp_rx_ring_size: 512\n";
        out << "xdp_tx_ring_size: 128\n";
        out << "xdp_fill_ring_size: 1024\n";
        out << "xdp_completion_ring_size: 512\n";
        out << "xdp_frame_size: 4096\n";
        out << "xdp_frame_count: 8192\n";
        out << "xdp_poll_timeout_ms: 0\n";
        out << "[dpdk]\n";
        out << "rx_desc = 1024\n";
        out << "tx_desc = 512\n";
    }

    const rxtech::RxConfig config = rxtech::load_config_file(path);
    assert(config.dpdk_rx_desc == 1024U);
    assert(config.dpdk_tx_desc == 512U);
    std::remove(path);
    return 0;
}
