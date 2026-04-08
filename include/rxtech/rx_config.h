#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech
{

    struct RxConfig
    {
        // ========== 基础配置 ==========
        std::string backend_name = "dpdk"; ///< 后端名称，默认为 dpdk
        std::string config_path;           ///< 配置文件路径
        std::string run_label;             ///< 运行标签
        std::vector<int> cpu_cores;        ///< CPU 核心列表

        // ========== 网络接口配置 ==========
        std::string interface_name = "receiver0"; ///< 网络接口名称，默认为 receiver0
        std::string receiver_ipv4;                ///< 接收端 IPv4 地址
        std::string allowed_source_ipv4;          ///< 允许的源 IPv4 地址
        std::uint32_t allowed_dest_port = 0;      ///< 允许的目标端口号
        std::uint32_t queue_id = 0;               ///< 队列 ID，默认为 0

        // ========== Linux Socket 后端配置 ==========
        std::string socket_bind_ip;                     ///< Linux socket 绑定 IPv4 地址，为空时回退到 receiver_ipv4
        std::uint32_t socket_bind_port = 0;             ///< Linux socket 绑定端口，为 0 时回退到 allowed_dest_port
        std::uint32_t socket_rcvbuf_bytes = 134217728U; ///< Linux socket 接收缓冲大小（字节），默认为 128 MiB
        bool socket_nonblocking = false;                ///< Linux socket 是否启用非阻塞模式，默认为 false
        std::uint32_t socket_batch_timeout_ms = 10;     ///< Linux socket 每轮批量收包超时（毫秒），默认为 10

        // ========== DPDK 后端配置 ==========
        std::string dpdk_pci_addr;                ///< DPDK PCI 地址
        std::uint32_t dpdk_port_id = 0;           ///< DPDK 端口 ID，默认为 0
        std::uint32_t dpdk_socket_mem_mb = 1024;  ///< DPDK Socket 内存大小（MB），默认为 1024
        std::uint32_t dpdk_mempool_size = 4096;   ///< DPDK 内存池大小，默认为 4096
        std::uint32_t dpdk_mbuf_cache_size = 256; ///< DPDK mbuf 缓存大小，默认为 256
        std::uint32_t dpdk_rx_desc = 256;         ///< DPDK 接收描述符数量，默认为 256
        std::uint32_t dpdk_tx_desc = 256;         ///< DPDK 发送描述符数量，默认为 256

        // ========== 数据接收配置 ==========
        std::uint32_t max_burst = 64;        ///< 最大突发数据包数量，默认为 64
        std::uint32_t packet_size_bytes = 0; ///< 数据包大小（字节），0 表示自动
        std::uint32_t duration_seconds = 0;  ///< 运行持续时间（秒），0 表示无限
        bool run_until_stopped = false;      ///< 是否持续运行直到手动停止，默认为 false

        // ========== 协议解析配置 ==========
        std::uint32_t protocol_udp_packet_size = 2048;  ///< 协议 UDP 数据包大小（字节），默认为 2048
        std::uint32_t protocol_channels_per_prt = 3;    ///< 每个 PRT 的通道数，默认为 3
        std::uint32_t protocol_packets_per_channel = 9; ///< 每个通道的数据包数，默认为 9
        std::uint32_t protocol_expected_n_prt = 0;      ///< 期望的 PRT 数量，0 表示动态
        std::uint32_t protocol_max_n_prt = 100U;        ///< 最大 PRT 数量，默认为 100
        std::uint64_t protocol_cpi_timeout_ns = 0;      ///< CPI 超时时间（纳秒），0 表示禁用
        bool protocol_dynamic_prt_enabled = true;       ///< 是否启用动态 PRT，默认为 true

        // ========== 抓包配置 ==========
        bool capture_enabled = true;                               ///< 是否启用抓包功能，默认为 true
        std::string capture_output_dir = "results";                ///< 抓包数据输出目录，默认为 results
        std::string capture_index_filename = "capture_index.csv";  ///< 抓包索引文件名
        std::string capture_data_filename = "capture_packets.bin"; ///< 抓包数据文件名

        // ========== 原始帧记录配置 ==========
        bool raw_record_enabled = false;                                     ///< 是否启用原始帧记录，默认为 false
        std::string raw_record_output_dir = "/data/rx_tech_demo/raw_frames"; ///< 原始帧记录输出目录
        std::string raw_record_file_prefix = "radar_raw";                    ///< 原始帧记录文件前缀
        std::uint32_t raw_record_ring_slots = 4096;                          ///< 原始帧记录环形缓冲区槽位数，默认为 4096
        std::uint32_t raw_record_writer_batch_size = 64;                     ///< 原始帧写入器批量大小，默认为 64
        std::uint32_t raw_record_max_frame_bytes = 16384;                    ///< 原始帧最大字节数，默认为 16384
        std::uint64_t raw_record_segment_bytes = 5368709120ULL / 10ULL;      ///< 原始帧分段大小（字节），默认为 512MB
        std::uint64_t raw_record_max_total_bytes = 5368709120ULL;            ///< 原始帧最大总大小（字节），默认为 5GB

        // ========== 反馈系统配置 ==========
        bool feedback_enabled = false;               ///< 是否启用反馈功能，默认为 false
        std::string feedback_host;                   ///< 反馈服务器主机地址
        std::string feedback_bind_host;              ///< 反馈绑定主机地址
        std::uint32_t feedback_port = 0;             ///< 反馈端口号
        std::uint32_t feedback_interval_seconds = 1; ///< 反馈间隔（秒），默认为 1

        // ========== 日志配置 ==========
        std::string log_level = "info";    ///< 日志级别，默认为 info
        std::string log_output = "stdout"; ///< 日志输出方式，默认为 stdout
        std::string log_file_path;         ///< 日志文件路径

        // ========== 输出与监控配置 ==========
        std::string output_dir = "results";         ///< 输出目录，默认为 results
        std::uint32_t status_interval_seconds = 10; ///< 状态报告间隔（秒），默认为 10
        bool metrics_detail_enabled = false;        ///< 是否启用详细指标，默认为 false
        bool run_artifacts_prepared = false;        ///< 运行产物是否已准备，默认为 false
    };

    // ========== 配置管理接口 ==========
    RxConfig load_default_config();                                   ///< 加载默认配置
    RxConfig load_config_file(const std::string &path);               ///< 从文件加载配置
    void merge_config(RxConfig &base, const RxConfig &overrides);     ///< 合并配置（覆盖模式）
    std::string effective_socket_bind_ip(const RxConfig &config);     ///< 计算 Linux socket 的实际绑定 IPv4
    std::uint16_t effective_socket_bind_port(const RxConfig &config); ///< 计算 Linux socket 的实际绑定端口

} // namespace rxtech
