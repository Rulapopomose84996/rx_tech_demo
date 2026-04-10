#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "rxtech/rx_config.h"

int main()
{
    rxtech::RxConfig base = rxtech::load_default_config();
    rxtech::RxConfig overrides = rxtech::load_default_config();
    overrides.process.backend_name.clear();
    overrides.operations.output_dir = "results/merged-legacy";
    overrides.capture.capture_output_dir = "results/merged";
    overrides.capture.capture_index_filename = "merged.csv";
    overrides.capture.capture_data_filename = "merged.bin";
    overrides.ingress.queue_id = 3U;
    overrides.runtime.max_burst = 32U;
    overrides.runtime.duration_seconds = 7U;
    overrides.process.cpu_cores = {16, 17};
    overrides.ingress.allowed_source_ipv4 = "172.20.11.222";
    overrides.ingress.allowed_dest_port = 9999U;
    overrides.ingress.socket_bind_ip = "0.0.0.0";
    overrides.ingress.socket_bind_port = 10000U;
    overrides.ingress.socket_rcvbuf_bytes = 8388608U;
    overrides.ingress.socket_nonblocking = true;
    overrides.ingress.socket_batch_timeout_ms = 25U;
    overrides.capture.capture_enabled = false;
    overrides.operations.log_level = "debug";
    overrides.operations.log_output = "file";
    overrides.operations.log_file_path = "logs/merged.log";
    overrides.protocol.udp_packet_size = 4096U;
    overrides.protocol.channels_per_prt = 4U;
    overrides.protocol.packets_per_channel = 7U;

    rxtech::merge_config(base, overrides);
    assert(base.operations.output_dir == "results/merged-legacy");
    assert(base.capture.capture_output_dir == "results/merged");
    assert(base.capture.capture_index_filename == "merged.csv");
    assert(base.capture.capture_data_filename == "merged.bin");
    assert(base.ingress.queue_id == 3U);
    assert(base.runtime.max_burst == 32U);
    assert(base.runtime.duration_seconds == 7U);
    assert(base.process.cpu_cores.size() == 2U);
    assert(base.process.cpu_cores[1] == 17);
    assert(base.ingress.allowed_source_ipv4 == "172.20.11.222");
    assert(base.ingress.allowed_dest_port == 9999U);
    assert(base.ingress.socket_bind_ip == "0.0.0.0");
    assert(base.ingress.socket_bind_port == 10000U);
    assert(base.ingress.socket_rcvbuf_bytes == 8388608U);
    assert(base.ingress.socket_nonblocking);
    assert(base.ingress.socket_batch_timeout_ms == 25U);
    assert(!base.capture.capture_enabled);
    assert(base.operations.log_level == "debug");
    assert(base.operations.log_output == "file");
    assert(base.operations.log_file_path == "logs/merged.log");
    assert(base.protocol.udp_packet_size == 4096U);
    assert(base.protocol.channels_per_prt == 4U);
    assert(base.protocol.packets_per_channel == 7U);
    return 0;
}
