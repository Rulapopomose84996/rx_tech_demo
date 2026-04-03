#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech
{

    struct RxConfig
    {
        std::string backend_name = "dpdk";
        std::string config_path;
        std::string output_dir = "results";
        std::string capture_output_dir = "results";
        std::string capture_index_filename = "capture_index.csv";
        std::string capture_data_filename = "capture_packets.bin";
        std::string raw_record_output_dir = "/data/rx_tech_demo/raw_frames";
        std::string raw_record_file_prefix = "radar_raw";
        std::string interface_name = "receiver0";
        std::string receiver_ipv4;
        std::string allowed_source_ipv4;
        std::string dpdk_pci_addr;
        std::string feedback_host;
        std::string feedback_bind_host;
        std::string log_level = "info";
        std::string log_output = "stdout";
        std::string log_file_path;
        std::uint32_t queue_id = 0;
        std::uint32_t max_burst = 64;
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
        std::uint32_t protocol_udp_packet_size = 2048;
        std::uint32_t protocol_channels_per_prt = 3;
        std::uint32_t protocol_packets_per_channel = 9;
        std::uint32_t protocol_expected_n_prt = 0;
        std::uint64_t protocol_cpi_timeout_ns = 0;
        std::uint32_t raw_record_ring_slots = 4096;
        std::uint32_t raw_record_writer_batch_size = 64;
        std::uint32_t raw_record_max_frame_bytes = 16384;
        std::uint64_t raw_record_segment_bytes = 5368709120ULL / 10ULL;
        std::uint64_t raw_record_max_total_bytes = 5368709120ULL;
        bool run_until_stopped = false;
        bool capture_enabled = true;
        bool raw_record_enabled = false;
        bool feedback_enabled = false;
        bool run_artifacts_prepared = false;
        bool metrics_detail_enabled = false;
        bool protocol_dynamic_prt_enabled = true;
        std::uint32_t protocol_max_n_prt = 100U;
        std::string run_label;
        std::vector<int> cpu_cores;
    };

    RxConfig load_default_config();
    RxConfig load_config_file(const std::string &path);
    void merge_config(RxConfig &base, const RxConfig &overrides);

} // namespace rxtech
