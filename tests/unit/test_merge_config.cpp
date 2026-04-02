#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "rxtech/rx_config.h"

int main()
{
    rxtech::RxConfig base = rxtech::load_default_config();
    rxtech::RxConfig overrides = rxtech::load_default_config();
    overrides.backend_name.clear();
    overrides.output_dir = "results/merged-legacy";
    overrides.capture_output_dir = "results/merged";
    overrides.capture_index_filename = "merged.csv";
    overrides.capture_data_filename = "merged.bin";
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
    overrides.capture_enabled = false;
    overrides.log_level = "debug";
    overrides.log_output = "file";
    overrides.log_file_path = "logs/merged.log";
    overrides.protocol_udp_packet_size = 4096U;
    overrides.protocol_channels_per_prt = 4U;
    overrides.protocol_packets_per_channel = 7U;

    rxtech::merge_config(base, overrides);
    assert(base.output_dir == "results/merged-legacy");
    assert(base.capture_output_dir == "results/merged");
    assert(base.capture_index_filename == "merged.csv");
    assert(base.capture_data_filename == "merged.bin");
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
    assert(!base.capture_enabled);
    assert(base.log_level == "debug");
    assert(base.log_output == "file");
    assert(base.log_file_path == "logs/merged.log");
    assert(base.protocol_udp_packet_size == 4096U);
    assert(base.protocol_channels_per_prt == 4U);
    assert(base.protocol_packets_per_channel == 7U);
    return 0;
}
