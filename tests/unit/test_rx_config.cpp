#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>

#include "rxtech/rx_config.h"

int main()
{
    const rxtech::RxConfig default_config = rxtech::load_default_config();
    if (default_config.ingress.interface_name != "receiver0")
    {
        std::cerr << "expected default interface receiver0, got " << default_config.ingress.interface_name << '\n';
        return 1;
    }
    if (default_config.capture.capture_output_dir != "results" ||
        default_config.capture.capture_index_filename != "capture_index.csv" ||
        default_config.capture.capture_data_filename != "capture_packets.bin")
    {
        std::cerr << "unexpected default capture configuration\n";
        return 1;
    }

    // Verify output policy defaults
    if (default_config.operations.output_drop_policy != rxtech::OutputDropPolicy::degrade ||
        default_config.operations.output_ring_capacity != 32U || default_config.operations.recycle_ring_capacity != 32U)
    {
        std::cerr << "unexpected default output policy configuration\n";
        return 1;
    }

    {
        rxtech::RxConfig invalid = rxtech::load_default_config();
        invalid.process.backend_name = "socket";
        invalid.operations.output_dir.clear();
        invalid.capture.capture_output_dir.clear();
        invalid.capture.capture_index_filename.clear();
        invalid.capture.capture_data_filename.clear();
        invalid.capture.raw_record_enabled = true;
        invalid.capture.raw_record_output_dir.clear();
        invalid.capture.raw_record_file_prefix.clear();
        invalid.capture.raw_record_ring_slots = 0U;
        invalid.capture.raw_record_writer_batch_size = 0U;
        invalid.capture.raw_record_max_frame_bytes = 0U;
        invalid.capture.raw_record_segment_bytes = 0U;
        invalid.capture.raw_record_max_total_bytes = 0U;
        invalid.ingress.allowed_dest_port = 70000U;
        invalid.ingress.socket_bind_port = 70001U;
        invalid.ingress.socket_rcvbuf_bytes = 0U;
        invalid.operations.log_output = "file";
        invalid.operations.log_file_path.clear();
        invalid.operations.structured_log_output = "file";
        invalid.operations.structured_log_file_path.clear();
        invalid.operations.metrics_export_mode = "json_socket";
        invalid.operations.metrics_export_path.clear();
        invalid.protocol.udp_packet_size = 0U;
        invalid.protocol.channels_per_prt = 0U;
        invalid.protocol.packets_per_channel = 0U;

        const std::vector<std::string> errors = rxtech::validate_config(invalid);
        const auto contains_error = [&](const char *message)
        { return std::find(errors.begin(), errors.end(), std::string(message)) != errors.end(); };

        if (!contains_error("allowed_dest_port 必须小于或等于 65535") ||
            !contains_error("backend=socket 时，socket_bind_ip 或 receiver_ipv4 不能为空") ||
            !contains_error("backend=socket 时，socket_bind_port 必须小于或等于 65535") ||
            !contains_error("backend=socket 时，socket_rcvbuf_bytes 必须大于 0") ||
            !contains_error("启用数据捕获时，capture_output_dir 不能为空") ||
            !contains_error("启用数据捕获时，capture_index_filename 不能为空") ||
            !contains_error("启用数据捕获时，capture_data_filename 不能为空") ||
            !contains_error("启用原始记录时，raw_record_output_dir 不能为空") ||
            !contains_error("启用原始记录时，raw_record_file_prefix 不能为空") ||
            !contains_error("当日志输出模式为 file 时，log_file_path 不能为空") ||
            !contains_error("当 structured_log_output 为 file 时，structured_log_file_path 不能为空") ||
            !contains_error("启用 metrics_export_mode 时，metrics_export_path 不能为空") ||
            !contains_error("protocol_udp_packet_size 必须大于 0") ||
            !contains_error("protocol_channels_per_prt 必须大于 0") ||
            !contains_error("protocol_packets_per_channel 必须大于 0"))
        {
            std::cerr << "validate_config should accumulate invalid configuration errors\n";
            return 1;
        }
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
        out << "structured_output = file\n";
        out << "structured_file_path = logs/rx.structured.jsonl\n";
        out << "structured_format = text\n";
        out << "rate_limit_seconds = 7\n";
        out << "[metrics]\n";
        out << "export_mode = prometheus_text\n";
        out << "export_path = metrics/current.prom\n";
        out << "export_interval_seconds = 3\n";
        out << "[protocol]\n";
        out << "udp_packet_size = 2048\n";
        out << "channels_per_prt = 3\n";
        out << "packets_per_channel = 9\n";
    }

    const rxtech::RxConfig config = rxtech::load_config_file(path);
    if (config.process.backend_name != "socket" || config.capture.capture_output_dir != "results/config_case" ||
        config.operations.output_dir != "results/config_case" || !config.capture.capture_enabled ||
        config.capture.capture_index_filename != "parsed.csv" || config.capture.capture_data_filename != "parsed.bin" ||
        !config.capture.raw_record_enabled ||
        config.capture.raw_record_output_dir != "/data/rx_tech_demo/test_raw_frames" ||
        config.capture.raw_record_file_prefix != "phase3_raw" || config.capture.raw_record_ring_slots != 2048U ||
        config.capture.raw_record_writer_batch_size != 32U || config.capture.raw_record_max_frame_bytes != 12288U ||
        config.capture.raw_record_segment_bytes != 268435456ULL ||
        config.capture.raw_record_max_total_bytes != 5368709120ULL || config.ingress.interface_name != "receiver1" ||
        config.ingress.queue_id != 22U || config.ingress.socket_bind_ip != "0.0.0.0" ||
        config.ingress.socket_bind_port != 10000U || config.ingress.socket_rcvbuf_bytes != 8388608U ||
        !config.ingress.socket_nonblocking || config.ingress.socket_batch_timeout_ms != 25U ||
        config.runtime.duration_seconds != 12U || config.runtime.max_burst != 32U ||
        config.runtime.packet_size_bytes != 1024U || config.process.cpu_cores.size() != 3U ||
        config.process.cpu_cores[0] != 16 || !config.runtime.run_until_stopped ||
        config.operations.status_interval_seconds != 10U || config.ingress.allowed_source_ipv4 != "172.20.11.222" ||
        config.ingress.allowed_dest_port != 9999U || config.operations.log_level != "debug" ||
        config.operations.log_output != "file" || config.operations.log_file_path != "logs/rx.log" ||
        config.operations.structured_log_output != "file" ||
        config.operations.structured_log_file_path != "logs/rx.structured.jsonl" ||
        config.operations.structured_log_format != "text" || config.operations.log_rate_limit_seconds != 7U ||
        config.operations.metrics_export_mode != "prometheus_text" ||
        config.operations.metrics_export_path != "metrics/current.prom" ||
        config.operations.metrics_export_interval_seconds != 3U || config.protocol.udp_packet_size != 2048U ||
        config.protocol.channels_per_prt != 3U || config.protocol.packets_per_channel != 9U)
    {
        std::cerr << "config parsing regression in test_rx_config\n";
        return 1;
    }
    if (rxtech::effective_socket_bind_ip(config) != "0.0.0.0" || rxtech::effective_socket_bind_port(config) != 10000U)
    {
        std::cerr << "socket bind override regression in test_rx_config\n";
        return 1;
    }

    const rxtech::RxConfig dpdk_config = rxtech::load_config_file("configs/dpdk_single_face.conf");
    if (dpdk_config.process.backend_name != "dpdk" || dpdk_config.ingress.interface_name != "receiver0" ||
        dpdk_config.ingress.receiver_ipv4 != "172.20.11.100" ||
        dpdk_config.ingress.allowed_source_ipv4 != "172.20.11.222" || dpdk_config.ingress.allowed_dest_port != 9999U ||
        dpdk_config.ingress.dpdk_pci_addr != "0001:05:00.0")
    {
        std::cerr << "dpdk single face config should point at receiver0 / 172.20.11.100 / 172.20.11.222 / 9999 / "
                     "0001:05:00.0\n";
        return 1;
    }
    if (rxtech::effective_socket_bind_ip(dpdk_config) != "172.20.11.100" ||
        rxtech::effective_socket_bind_port(dpdk_config) != 9999U)
    {
        std::cerr << "socket bind fallback should reuse receiver_ipv4 / allowed_dest_port\n";
        return 1;
    }

    // Verify output policy config file parsing
    {
        const rxtech::RxConfig loaded = rxtech::load_config_file("tests/data/output_policy.conf");
        if (loaded.operations.output_drop_policy != rxtech::OutputDropPolicy::error ||
            loaded.operations.output_ring_capacity != 64U || loaded.operations.recycle_ring_capacity != 128U)
        {
            std::cerr << "output policy config parsing regression\n";
            return 1;
        }
    }

    std::remove(path);
    return 0;
}
