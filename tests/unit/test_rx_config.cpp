#include <cstdio>
#include <fstream>
#include <iostream>

#include "rxtech/rx_config.h"

int main()
{
    const rxtech::RxConfig default_config = rxtech::load_default_config();
    if (default_config.interface_name != "receiver0")
    {
        std::cerr << "expected default interface receiver0, got "
                  << default_config.interface_name << '\n';
        return 1;
    }
    if (default_config.capture_output_dir != "results" ||
        default_config.capture_index_filename != "capture_index.csv" ||
        default_config.capture_data_filename != "capture_packets.bin")
    {
        std::cerr << "unexpected default capture configuration\n";
        return 1;
    }

    const char *path = "test_rx_config_generated.conf";
    {
        std::ofstream out(path, std::ios::trunc);
        out << "backend: socket\n";
        out << "output_dir: results/legacy_should_not_win\n";
        out << "xdp_bind_mode: copy\n";
        out << "xdp_rx_ring_size: 512\n";
        out << "xdp_tx_ring_size: 128\n";
        out << "xdp_fill_ring_size: 1024\n";
        out << "xdp_completion_ring_size: 512\n";
        out << "xdp_frame_size: 4096\n";
        out << "xdp_frame_count: 8192\n";
        out << "xdp_poll_timeout_ms: 0\n";
        out << "packet_size_bytes: 1024\n";
        out << "[capture]\n";
        out << "output_dir = results/config_case\n";
        out << "enabled = true\n";
        out << "index_filename = parsed.csv\n";
        out << "data_filename = parsed.bin\n";
        out << "[raw_record]\n";
        out << "enabled = true\n";
        out << "output_dir = /data/rx_tech_demo/test_raw_frames\n";
        out << "file_prefix = phase3_raw\n";
        out << "ring_slots = 2048\n";
        out << "writer_batch_size = 32\n";
        out << "max_frame_bytes = 12288\n";
        out << "segment_bytes = 268435456\n";
        out << "max_total_bytes = 5368709120\n";
        out << "[network]\n";
        out << "interface_name = receiver1\n";
        out << "queue_id = 22\n";
        out << "receiver_ipv4 = 172.20.11.100\n";
        out << "allowed_source_ipv4 = 172.20.11.222\n";
        out << "allowed_dest_port = 9999\n";
        out << "[socket]\n";
        out << "bind_ip = 0.0.0.0\n";
        out << "bind_port = 10000\n";
        out << "rcvbuf_bytes = 8388608\n";
        out << "nonblocking = true\n";
        out << "batch_timeout_ms = 25\n";
        out << "[runtime]\n";
        out << "duration_seconds = 12\n";
        out << "max_burst = 32\n";
        out << "cpu_cores = [16,17,18]\n";
        out << "run_until_stopped = true\n";
        out << "status_interval_seconds = 10\n";
        out << "[log]\n";
        out << "level = debug\n";
        out << "output = file\n";
        out << "file_path = logs/rx.log\n";
        out << "[protocol]\n";
        out << "udp_packet_size = 2048\n";
        out << "channels_per_prt = 3\n";
        out << "packets_per_channel = 9\n";
    }

    const rxtech::RxConfig config = rxtech::load_config_file(path);
    if (config.backend_name != "socket" || config.capture_output_dir != "results/config_case" ||
        config.output_dir != "results/config_case" ||
        !config.capture_enabled || config.capture_index_filename != "parsed.csv" ||
        config.capture_data_filename != "parsed.bin" ||
        !config.raw_record_enabled || config.raw_record_output_dir != "/data/rx_tech_demo/test_raw_frames" ||
        config.raw_record_file_prefix != "phase3_raw" ||
        config.raw_record_ring_slots != 2048U || config.raw_record_writer_batch_size != 32U ||
        config.raw_record_max_frame_bytes != 12288U ||
        config.raw_record_segment_bytes != 268435456ULL ||
        config.raw_record_max_total_bytes != 5368709120ULL ||
        config.interface_name != "receiver1" || config.queue_id != 22U ||
        config.socket_bind_ip != "0.0.0.0" || config.socket_bind_port != 10000U ||
        config.socket_rcvbuf_bytes != 8388608U || !config.socket_nonblocking ||
        config.socket_batch_timeout_ms != 25U ||
        config.duration_seconds != 12U || config.max_burst != 32U ||
        config.packet_size_bytes != 1024U || config.cpu_cores.size() != 3U ||
        config.cpu_cores[0] != 16 || !config.run_until_stopped ||
        config.status_interval_seconds != 10U ||
        config.allowed_source_ipv4 != "172.20.11.222" || config.allowed_dest_port != 9999U ||
        config.log_level != "debug" || config.log_output != "file" || config.log_file_path != "logs/rx.log" ||
        config.protocol_udp_packet_size != 2048U || config.protocol_channels_per_prt != 3U ||
        config.protocol_packets_per_channel != 9U)
    {
        std::cerr << "config parsing regression in test_rx_config\n";
        return 1;
    }
    if (rxtech::effective_socket_bind_ip(config) != "0.0.0.0" ||
        rxtech::effective_socket_bind_port(config) != 10000U)
    {
        std::cerr << "socket bind override regression in test_rx_config\n";
        return 1;
    }

    const rxtech::RxConfig dpdk_config = rxtech::load_config_file("configs/dpdk_single_face.conf");
    if (dpdk_config.backend_name != "dpdk" || dpdk_config.interface_name != "receiver0" ||
        dpdk_config.receiver_ipv4 != "172.20.11.100" ||
        dpdk_config.allowed_source_ipv4 != "172.20.11.222" ||
        dpdk_config.allowed_dest_port != 9999U ||
        dpdk_config.dpdk_pci_addr != "0001:05:00.0")
    {
        std::cerr << "dpdk single face config should point at receiver0 / 172.20.11.100 / 172.20.11.222 / 9999 / 0001:05:00.0\n";
        return 1;
    }
    if (rxtech::effective_socket_bind_ip(dpdk_config) != "172.20.11.100" ||
        rxtech::effective_socket_bind_port(dpdk_config) != 9999U)
    {
        std::cerr << "socket bind fallback should reuse receiver_ipv4 / allowed_dest_port\n";
        return 1;
    }
    std::remove(path);
    return 0;
}
