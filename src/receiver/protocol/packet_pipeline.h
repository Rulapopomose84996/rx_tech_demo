#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>

#include "rxtech/metrics.h"
#include "rxtech/packet_desc.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/rx_config.h"
#include "rxtech/udp_payload_assembler.h"
#include "protocol_pipeline_types.h"

namespace rxtech
{
    class UdpDatagramPipeline;

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
        ~PacketPipeline();

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
        UdpPayloadAssembler assembler_;                    ///< UDP载荷组装器，负责IP分片重组和UDP载荷提取
        std::unique_ptr<UdpDatagramPipeline> datagram_pipeline_; ///< datagram-first 协议入口
    };

} // namespace rxtech
