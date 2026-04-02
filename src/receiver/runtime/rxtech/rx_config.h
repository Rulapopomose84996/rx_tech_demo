#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech {

struct RxConfig {
    std::string backend_name = "af_xdp";
    std::string config_path;
    std::string output_dir = "results";
    std::string interface_name = "receiver0";
    std::string receiver_ipv4;
    std::string allowed_source_ipv4;
    std::string dpdk_pci_addr;
    std::string xdp_bind_mode = "auto";
    std::string feedback_host;
    std::string feedback_bind_host;
    std::uint32_t queue_id = 0;
    std::uint32_t max_burst = 64;
    std::uint32_t xdp_rx_ring_size = 1024;
    std::uint32_t xdp_tx_ring_size = 256;
    std::uint32_t xdp_fill_ring_size = 2048;
    std::uint32_t xdp_completion_ring_size = 2048;
    std::uint32_t xdp_frame_size = 2048;
    std::uint32_t xdp_frame_count = 4096;
    std::uint32_t xdp_poll_timeout_ms = 0;
    std::uint32_t duration_seconds = 0;
    std::uint32_t packet_size_bytes = 0;
    std::uint32_t status_interval_seconds = 10;
    std::uint32_t feedback_interval_seconds = 1;
    std::uint32_t feedback_port = 0;
    std::uint32_t allowed_dest_port = 0;
    std::uint32_t dpdk_port_id = 0;
    std::uint32_t dpdk_socket_mem_mb = 1024;
    std::uint32_t dpdk_mempool_size = 4096;
    std::uint32_t dpdk_mbuf_cache_size = 256;
    std::uint32_t dpdk_rx_desc = 256;
    std::uint32_t dpdk_tx_desc = 256;
    bool run_until_stopped = false;
    bool feedback_enabled = false;
    std::vector<int> cpu_cores;
};

RxConfig load_default_config();
RxConfig load_config_file(const std::string& path);
void merge_config(RxConfig& base, const RxConfig& overrides);

}  // namespace rxtech
