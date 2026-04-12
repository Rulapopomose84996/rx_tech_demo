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
     * @brief 数据包处理管道类 (legacy frame adapter)
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
        PacketPipeline(const RxConfig &config, const ProtocolSpec &spec);
        ~PacketPipeline();

        template <typename Callback>
        PacketProcessStats process_packet(const PacketDesc &packet, MetricsCollector &metrics,
                                          std::ostream *diagnostic_output, std::uint32_t &invalid_dumped,
                                          Callback &&on_packet);

      private:
        PacketProcessStats process_packet_impl(const PacketDesc &packet, MetricsCollector &metrics,
                                               std::ostream *diagnostic_output, std::uint32_t &invalid_dumped,
                                               const std::function<void(const ProcessedPacket &)> &on_packet);

        UdpPayloadAssembler assembler_;
        std::unique_ptr<UdpDatagramPipeline> datagram_pipeline_;
    };

    template <typename Callback>
    PacketProcessStats PacketPipeline::process_packet(const PacketDesc &packet, MetricsCollector &metrics,
                                                      std::ostream *diagnostic_output, std::uint32_t &invalid_dumped,
                                                      Callback &&on_packet)
    {
        return process_packet_impl(packet, metrics, diagnostic_output, invalid_dumped,
                                   std::forward<Callback>(on_packet));
    }

} // namespace rxtech
