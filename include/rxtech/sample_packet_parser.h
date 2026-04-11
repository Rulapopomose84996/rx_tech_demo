#pragma once

#include <cstddef>
#include <cstdint>

#include "rxtech/packet_desc.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/udp_payload_assembler.h"

namespace rxtech
{

    /**
     * @brief 数据包类型枚举
     *
     * 用于标识解析后的数据包种类
     */
    enum class PacketKind
    {
        unknown,       ///< 未知类型
        control_table, ///< 控制表
        data_packet    ///< 数据包
    };

    /**
     * @brief 数据包拒绝原因枚举
     *
     * 用于标识数据包解析失败的具体原因
     */
    enum class RejectReason
    {
        none,                 ///< 无错误，解析成功
        invalid_len,          ///< 长度无效
        invalid_header,       ///< 头部无效
        invalid_channel,      ///< 通道无效
        invalid_prt,          ///< PRT 超出合法范围
        invalid_packet_index, ///< 包索引无效
        invalid_tail,         ///< 尾部无效
        invalid_field_combo,  ///< 字段组合无效
        truncated_datagram    ///< datagram 在 ingress 层已被截断
    };

    constexpr std::size_t kRejectReasonCount = static_cast<std::size_t>(RejectReason::truncated_datagram) + 1U;

    /**
     * @brief 解析后的数据包视图结构体
     *
     * 包含数据包解析后的所有关键信息，用于后续处理
     */
    struct ParsedPacketView
    {
        bool valid = false;                    ///< 标识数据包是否有效
        PacketKind kind = PacketKind::unknown; ///< 数据包类型
        std::uint16_t cpi = 0;                 ///< CPI (Control Packet Index) 值
        std::uint16_t channel = 0;             ///< 通道号
        std::uint16_t prt = 0;                 ///< PRT (Pulse Repetition Time) 值
        std::uint16_t packet_index = 0;        ///< 数据包索引
        std::uint32_t tail = 0;                ///< 尾部校验值
        const std::uint8_t *payload_ptr =
            nullptr; ///< 载荷数据指针；生存期绑定到底层 burst/frame 存储，release_burst() 或源 frame 销毁后立即失效
        std::uint32_t payload_len = 0;                   ///< 载荷数据长度
        std::uint64_t rx_tsc = 0;                        ///< 接收时间戳 (TSC)
        RejectReason reject_reason = RejectReason::none; ///< 解析失败时的拒绝原因
    };

    /**
     * @brief 数据包解析器类
     *
     * 用于解析雷达协议数据包，支持从 PacketDesc 和 UdpPayloadFrame 两种输入源进行解析
     * 根据 ProtocolSpec 中定义的协议规范来验证和解析数据包
     */
    class PacketParser
    {
      public:
        /**
         * @brief 默认构造函数
         */
        PacketParser() = default;

        /**
         * @brief 带协议规范的构造函数
         * @param spec 协议规范对象，用于定义数据包解析的规则和参数
         */
        explicit PacketParser(const ProtocolSpec &spec) : spec_(spec) {}

        /**
         * @brief 从 PacketDesc 解析数据包
         * @param packet 数据包描述对象，包含原始数据包信息
         * @return ParsedPacketView 解析后的数据包视图，包含解析结果和元数据
         * @note 该函数为无异常 (noexcept) 函数，解析失败时返回的视图中 valid 字段为 false
         */
        ParsedPacketView parse(const PacketDesc &packet) const noexcept;

        /**
         * @brief 从 UDP 载荷帧解析数据包
         * @param frame UDP 载荷帧对象，包含组装后的 UDP 载荷数据
         * @return ParsedPacketView 解析后的数据包视图，包含解析结果和元数据
         * @note 该函数为无异常 (noexcept) 函数，解析失败时返回的视图中 valid 字段为 false
         */
        ParsedPacketView parse(const UdpPayloadFrame &frame) const noexcept;

      private:
        ProtocolSpec spec_{}; ///< 协议规范对象，存储解析所需的协议配置
    };

    /**
     * @brief 获取数据包类型的字符串名称
     * @param kind 数据包类型枚举值
     * @return const char* 对应的类型名称字符串
     * @note 返回的是静态字符串，不需要释放内存
     */
    const char *packet_kind_name(PacketKind kind) noexcept;

    /**
     * @brief 获取拒绝原因的字符串名称
     * @param reason 拒绝原因枚举值
     * @return const char* 对应的原因名称字符串
     * @note 返回的是静态字符串，不需要释放内存
     */
    const char *reject_reason_name(RejectReason reason) noexcept;

} // namespace rxtech
