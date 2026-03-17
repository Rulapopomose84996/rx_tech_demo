#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech {

struct RxConfig {
    std::string backend_name = "socket";
    std::string mode_name = "rx_only";
    std::string scenario_path;
    std::string config_path;
    std::string output_dir = "results";
    std::string interface_name = "enP1s25f3";
    std::string bind_address = "127.0.0.1";
    std::string dpdk_pci_addr;
    std::string xdp_bind_mode = "auto";
    std::uint32_t queue_id = 0;
    std::uint32_t max_burst = 64;
    std::uint32_t duration_seconds = 0;
    std::uint16_t udp_port = 9999;
    std::uint32_t packet_size_bytes = 0;
    std::uint32_t socket_poll_timeout_ms = 50;
    std::uint32_t dpdk_port_id = 0;
    std::uint32_t dpdk_socket_mem_mb = 1024;
    std::uint32_t dpdk_mempool_size = 4096;
    std::uint32_t dpdk_mbuf_cache_size = 256;
    std::uint32_t dpdk_rx_desc = 256;
    std::uint32_t dpdk_tx_desc = 256;
    bool enable_internal_traffic = false;
    std::vector<int> cpu_cores;
};

RxConfig load_default_config();
RxConfig load_config_file(const std::string& path);
void merge_config(RxConfig& base, const RxConfig& overrides);

}  // namespace rxtech
