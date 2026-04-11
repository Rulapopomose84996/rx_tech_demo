#pragma once

#include <cstdint>

namespace rxtech
{

    struct RxConfig;

    /**
     * @brief 协议规范结构体，定义了雷达数据接收协议的常量参数
     *
     * 该结构体包含了UDP数据包格式、IQ数据处理以及PRT（脉冲重复时间）相关的协议规范。
     * 主要用于解析和处理雷达接收端的原始数据流。
     */
    struct ProtocolSpec
    {
        std::uint32_t magic_control = 0x55AAFF00U;  ///< 控制包魔数标识，用于识别控制类型的数据包
        std::uint32_t magic_data = 0x55AAFF03U;     ///< 数据包魔数标识，用于识别IQ数据类型的数据包
        std::uint32_t magic_tail = 0x55AAFF30U;     ///< 尾部魔数标识，用于数据包完整性校验
        std::uint32_t udp_packet_size = 2048U;      ///< UDP数据包总大小（字节）
        std::uint32_t packet_header_size = 16U;     ///< 数据包头部大小（字节），包含魔数等元数据
        std::uint32_t packet_data_size = 2032U;     ///< 数据包有效载荷大小（字节），等于udp_packet_size减去头部
        std::uint32_t channels_per_prt = 3U;        ///< 每个PRT（脉冲重复时间）包含的通道数量
        std::uint32_t packets_per_channel = 9U;     ///< 每个通道需要的数据包数量
        std::uint32_t iq_per_full_packet = 508U;    ///< 完整数据包中包含的IQ采样点数量
        std::uint32_t iq_per_last_packet = 476U;    ///< 最后一个数据包中包含的IQ采样点数量（通常较少）
        std::uint32_t control_table_size = 2048U;   ///< 控制表大小（字节）
        std::uint32_t expected_n_prt = 0U;          ///< 期望的PRT数量，0表示使用动态模式自动检测
        std::uint64_t protocol_cpi_timeout_ns = 0U; ///< CPI（相干处理间隔）超时时间（纳秒），0表示禁用超时
        bool dynamic_prt_enabled = true;            ///< 是否启用动态PRT检测模式
        std::uint32_t max_n_prt = 100U;             ///< 最大允许的PRT数量，用于限制内存分配和边界检查
    };

    /**
     * @brief 从配置对象创建协议规范实例
     *
     * 该函数根据RxConfig中的协议相关配置参数，构造并返回一个ProtocolSpec对象。
     * 主要用于将用户配置转换为内部协议处理所需的标准化参数。
     *
     * @param config 接收端配置对象，包含协议相关的配置参数
     * @return ProtocolSpec 根据配置生成的协议规范对象
     */
    ProtocolSpec protocol_spec_from_config(const RxConfig &config);

} // namespace rxtech
