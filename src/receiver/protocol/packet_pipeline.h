#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>

#include "rxtech/metrics.h"
#include "rxtech/packet_desc.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/rx_config.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/sample_packet_validator.h"
#include "rxtech/udp_payload_assembler.h"

namespace rxtech
{

    /**
     * @brief 处理后的数据包结构体
     * 
     * 该结构体封装了经过完整处理流程的数据包信息，包括UDP载荷帧、
     * 解析后的数据包视图和解释后的数据包视图，以及数据包的来源信息。
     */
    struct ProcessedPacket
    {
        UdpPayloadFrame udp_frame;              ///< UDP载荷帧，包含完整的UDP载荷数据和网络层信息
        ParsedPacketView parsed;                ///< 解析后的数据包视图，包含从原始数据中提取的协议字段
        InterpretedPacketView interpreted;      ///< 解释后的数据包视图，包含基于协议序列的语义化信息
        std::uint32_t source_queue_id = 0;      ///< 源队列ID，标识数据包来自哪个接收队列
        std::uint64_t source_ts_ns = 0;         ///< 源时间戳（纳秒），记录数据包到达的时间
    };

    /**
     * @brief 数据包处理统计信息结构体
     * 
     * 用于跟踪和记录数据包处理过程中的关键指标，包括接受的数据量、
     * 数据包数量以及被过滤的数据包数量。
     */
    struct PacketProcessStats
    {
        std::uint64_t accepted_bytes = 0;       ///< 接受的总字节数
        std::size_t accepted_packets = 0;       ///< 接受的数据包数量
        std::uint64_t filtered_packets = 0;     ///< 被过滤的数据包数量
    };

    /**
     * @brief 数据包处理管道类
     * 
     * 该类实现了完整的数据包处理流水线，负责从原始数据包到最终处理结果的转换。
     * 处理流程包括：UDP载荷组装、数据包解析、有效性验证、协议序列解释等步骤。
     * 支持基于配置的过滤机制，可以只处理符合特定条件的数据包。
     * 
     * @note 该类不是线程安全的，每个线程应使用独立的实例
     */
    class PacketPipeline
    {
    public:
        /**
         * @brief 构造函数
         * 
         * 初始化数据包处理管道，配置所有必要的组件和过滤器。
         * 
         * @param config 接收配置对象，包含网络接口、IP地址、端口等配置信息
         * @param spec 协议规范对象，定义数据包的格式、魔数、大小等协议参数
         */
        PacketPipeline(const RxConfig &config, const ProtocolSpec &spec);

        /**
         * @brief 处理单个数据包
         * 
         * 这是数据包处理管道的核心方法，执行以下处理步骤：
         * 1. 将原始数据包组装为UDP载荷帧
         * 2. 检查数据包是否符合过滤条件（源IP、目标端口等）
         * 3. 解析UDP载荷，提取协议字段
         * 4. 验证数据包的有效性
         * 5. 解释数据包在协议序列中的位置和意义
         * 6. 收集处理指标和统计信息
         * 7. 如果提供诊断输出流，写入诊断信息
         * 8. 通过回调函数通知调用者处理结果
         * 
         * @param packet 数据包描述对象，包含原始数据包指针、长度、时间戳等信息
         * @param metrics 指标收集器引用，用于记录处理过程中的各种指标
         * @param diagnostic_output 可选的诊断输出流指针，如果非空则写入详细的诊断信息
         * @param invalid_dumped 输出参数，返回因无效而被丢弃的数据包数量
         * @param on_packet 回调函数，当数据包成功处理完成后调用，传入ProcessedPacket对象
         * 
         * @return PacketProcessStats 本次处理的统计信息，包括接受的字节数、数据包数和过滤数
         * 
         * @note 回调函数on_packet仅在数据包通过所有验证和过滤后才会被调用
         * @note 如果数据包不符合过滤条件或验证失败，不会调用回调函数
         */
        PacketProcessStats process_packet(const PacketDesc &packet,
                                          IMetricsCollector &metrics,
                                          std::ostream *diagnostic_output,
                                          std::uint32_t &invalid_dumped,
                                          const std::function<void(const ProcessedPacket &)> &on_packet);

    private:
        /**
         * @brief 检查UDP载荷帧是否符合数据包过滤条件
         * 
         * 根据配置的允许源IP地址和目标端口，判断给定的UDP载荷帧是否应该被处理。
         * 过滤条件包括：
         * - 源IPv4地址必须与配置的allowed_source_ipv4匹配（如果已配置）
         * - 目标端口必须与配置的allowed_dest_port匹配（如果已配置）
         * 
         * @param frame UDP载荷帧对象，包含源/目标IP地址和端口信息
         * 
         * @return bool 如果数据包符合过滤条件返回true，否则返回false
         */
        bool matches_packet_filter(const UdpPayloadFrame &frame) const;

        RxConfig config_;                           ///< 接收配置对象，存储网络和过滤配置
        ProtocolSpec spec_;                         ///< 协议规范对象，定义数据包格式和解析规则
        PacketParser parser_;                       ///< 数据包解析器，负责从UDP载荷中提取协议字段
        PacketValidator validator_;                 ///< 数据包验证器，检查解析后的数据包是否有效
        ProtocolSequenceInterpreter interpreter_;   ///< 协议序列解释器，解释数据包在序列中的位置
        UdpPayloadAssembler assembler_;             ///< UDP载荷组装器，负责IP分片重组和UDP载荷提取
        std::uint32_t allowed_source_ipv4_be_ = 0;  ///< 允许的源IPv4地址（大端序），0表示不过滤
        std::uint32_t receiver_ipv4_be_ = 0;        ///< 接收端IPv4地址（大端序），用于验证目标地址
    };

} // namespace rxtech
