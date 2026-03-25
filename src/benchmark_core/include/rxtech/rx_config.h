#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech {

struct RxConfig {
    std::string backend_name = "af_xdp";
    std::string mode_name = "rx_only";
    std::string scenario_path;
    std::string config_path;
    std::string output_dir = "results";
    std::string interface_name = "receiver0";
    std::string dpdk_pci_addr;
    std::string xdp_bind_mode = "auto";
    std::string feedback_host;
    std::uint32_t queue_id = 0;
    std::uint32_t max_burst = 64;
    std::uint32_t duration_seconds = 0;
    std::uint32_t packet_size_bytes = 0;
    std::uint32_t status_interval_seconds = 10;
    std::uint32_t feedback_port = 0;
    std::uint32_t dpdk_port_id = 0;
    std::uint32_t dpdk_socket_mem_mb = 1024;
    std::uint32_t dpdk_mempool_size = 4096;
    std::uint32_t dpdk_mbuf_cache_size = 256;
    std::uint32_t dpdk_rx_desc = 256;
    std::uint32_t dpdk_tx_desc = 256;
    std::uint32_t reassembly_timeout_ms = 1000;
    bool run_until_stopped = false;
    bool feedback_enabled = false;
    std::vector<int> cpu_cores;
};

RxConfig load_default_config();
RxConfig load_config_file(const std::string& path);
void merge_config(RxConfig& base, const RxConfig& overrides);

}  // namespace rxtech
