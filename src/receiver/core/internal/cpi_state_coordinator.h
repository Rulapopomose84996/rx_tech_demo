#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "rxtech/cpi_admission.h"
#include "rxtech/cpi_context_pool.h"
#include "rxtech/cpi_finalizer.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/progress_tracker.h"
#include "rxtech/rx_config.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/slot_writer.h"
#include "rxtech/spsc_ring.h"

namespace rxtech
{

    /**
     * @brief CPI处理结果结构体
     *
     * 用于返回CPI数据包处理的最终结果
     */
    struct CpiProcessResult
    {
        bool accepted = false; ///< 标识数据包是否被接受处理
    };

    /**
     * @brief CPI状态协调器类
     *
     * 负责管理CPI的完整生命周期，包括：
     * - CPI的准入判断和激活
     * - 数据包的接收、验证和写入
     * - 进度跟踪和超时检测
     * - CPI的完成和资源回收
     *
     * 该类维护两个CPI上下文：当前活动的CPI和前一个CPI，支持平滑的状态转换。
     * 通过环形缓冲区与外部组件进行通信，实现高效的无锁数据传输。
     */
    class CpiStateCoordinator
    {
      public:
        /**
         * @brief 默认构造函数
         */
        CpiStateCoordinator() = default;

        /**
         * @brief 带协议规范的构造函数
         *
         * @param spec 协议规范对象，定义数据包格式、通道配置等参数
         */
        explicit CpiStateCoordinator(const ProtocolSpec &spec)
            : slot_writer_(spec), progress_tracker_(spec), spec_(spec)
        {
        }

        /**
         * @brief 附加输出和回收环形缓冲区
         *
         * 建立CPI处理管道的输出通道和资源回收通道。
         * 输出环用于发送完成的CPI结果，回收环用于释放已处理的CPI上下文资源。
         *
         * @param output_ring 指向CPI输出环形缓冲区的指针，用于传递处理完成的CPI数据
         * @param recycle_ring 指向回收令牌环形缓冲区的指针，用于通知外部释放CPI上下文
         */
        void attach_rings(SpscRing<CpiOutput> *output_ring, SpscRing<ReleaseToken> *recycle_ring)
        {
            output_ring_ = output_ring;
            recycle_ring_ = recycle_ring;
        }

        /**
         * @brief 处理控制数据包
         *
         * 解析并处理控制类型的数据包，可能触发新CPI的创建或现有CPI的配置更新。
         * 控制包包含CPI的元数据信息，如CPI ID、PRT数量、通道配置等。
         *
         * @param parsed 已解析的数据包视图，包含控制包的详细信息
         */
        void process_control_packet(const ParsedPacketView &parsed);

        /**
         * @brief 检查活动CPI是否超时
         *
         * 根据当前时间戳判断活动CPI是否超过配置的超时阈值。
         * 如果超时，则自动完成该CPI并触发资源清理流程。
         *
         * @param now_ns 当前时间戳（纳秒），用于超时计算
         * @param metrics 指标收集器引用，用于记录超时事件和相关统计信息
         * @return bool 如果检测到超时并执行了完成操作则返回true，否则返回false
         */
        bool check_timeout(std::uint64_t now_ns, IMetricsCollector &metrics);

        /**
         * @brief 处理数据数据包
         *
         * 核心处理方法，负责将IQ数据写入到活动CPI的上下文中。
         * 执行以下关键步骤：
         * 1. 验证数据包的有效性和所属CPI
         * 2. 如果需要，激活新的CPI上下文
         * 3. 将数据写入对应的时隙位置
         * 4. 更新进度跟踪器
         * 5. 检查CPI是否已完成所有数据接收
         *
         * @param parsed 已解析的数据包视图，包含原始数据包信息
         * @param packet 解释后的数据包视图，包含协议层面的语义信息
         * @param spec 协议规范对象，提供数据包格式和布局规则
         * @param metrics 指标收集器引用，用于记录处理统计信息
         * @param run_status 运行状态字符串，用于返回当前处理状态描述
         * @param run_error 错误信息字符串，用于返回处理过程中的错误详情
         * @return CpiProcessResult 处理结果，标识数据包是否被成功接受
         */
        CpiProcessResult process_data_packet(const ParsedPacketView &parsed, const InterpretedPacketView &packet,
                                             const ProtocolSpec &spec, IMetricsCollector &metrics,
                                             std::string &run_status, std::string &run_error);

        /**
         * @brief 清空回收环形缓冲区并释放池槽位
         *
         * 遍历回收环中的所有令牌，将对应的CPI上下文索引标记为可用状态，
         * 使其可以被后续的CPI重新使用。这是资源管理的关键环节，防止内存泄漏。
         *
         * @param metrics 指标收集器引用，用于记录资源回收统计
         */
        void drain_recycle(IMetricsCollector &metrics);

        /**
         * @brief 为干净关闭完成活动和之前的CPI上下文
         *
         * 在系统关闭或重置时调用，确保所有未完成的CPI都被正确地完成和清理。
         * 依次完成活动CPI和前一个CPI（如果存在），避免资源泄漏和状态不一致。
         *
         * @param metrics 指标收集器引用，用于记录关闭时的统计信息
         */
        void finalize_active_for_shutdown(IMetricsCollector &metrics);

        /**
         * @brief 配置输出丢弃策略
         *
         * 设置 finalize 后输出 push 失败时的运行结论策略。
         * "degrade" 表示降级（默认），"error" 表示错误。
         *
         * @param policy 策略字符串，"degrade" 或 "error"
         */
        void configure_output_policy(OutputDropPolicy policy);

        /**
         * @brief 查询输出丢弃是否视为错误
         *
         * @return true 如果 output_drop_policy 为 error
         */
        bool output_drop_is_error() const;

        /**
         * @brief 查询是否已发生输出退化
         *
         * @return true 如果至少有一次输出丢弃
         */
        bool output_degraded() const
        {
            return output_degraded_.load(std::memory_order_relaxed);
        }

        /**
         * @brief 释放活动CPI上下文
         *
         * 立即释放当前活动的CPI上下文，将其返回到上下文池中。
         * 通常在紧急情况下或需要强制重置状态时使用。
         */
        void release_active();

      private:
        /**
         * @brief 打开新的活动CPI上下文
         *
         * 从上下文池中获取一个新的CPI上下文实例，初始化其状态，
         * 并将其设置为当前活动的CPI。同时处理与前一个CPI的关系。
         *
         * @param cpi_id CPI的唯一标识符
         * @param metrics 指标收集器引用，用于记录CPI激活事件
         * @param run_status 运行状态字符串，用于返回激活过程的状态
         * @param run_error 错误信息字符串，用于返回激活失败的详细原因
         * @return bool 如果成功激活新的CPI则返回true，否则返回false（如池耗尽）
         */
        bool open_active(std::uint16_t cpi_id, IMetricsCollector &metrics, std::string &run_status,
                         std::string &run_error);

        /**
         * @brief 完成活动CPI上下文
         *
         * 根据指定的触发条件完成当前活动的CPI，生成输出结果并发送到输出环。
         * 完成后，活动CPI变为前一个CPI，为下一个CPI腾出空间。
         *
         * @param trigger 触发完成的原因代码，如正常完成、超时、错误等
         * @param metrics 指标收集器引用，用于记录CPI完成事件和统计信息
         */
        void finalize_active(std::uint32_t trigger, IMetricsCollector &metrics);

        /**
         * @brief 完成前一个CPI上下文
         *
         * 清理前一个CPI的资源，将其上下文索引返回到回收环中。
         * 这确保了资源的及时释放，防止上下文池耗尽。
         *
         * @param trigger 触发完成的原因代码
         * @param metrics 指标收集器引用，用于记录资源释放事件
         */
        void finalize_previous(std::uint32_t trigger, IMetricsCollector &metrics);

        /**
         * @brief 绑定控制快照到活动CPI
         *
         * 将显式控制快照关联到活动CPI上下文中。
         * 快照包含CPI的配置参数，如PRT数量、通道数、超时时间等，
         * 这些信息用于后续的数据验证和处理。
         */
        void bind_snapshot_to_active(const ControlSnapshot &snapshot);

        CpiContextPool ctx_pool_;                        ///< CPI上下文池，管理所有可用的CPI上下文实例
        RecentClosedRing closed_ring_;                   ///< 最近关闭的CPI环形缓冲区，用于追踪刚完成的CPI以防止重复处理
        CpiAdmission admission_;                         ///< CPI准入控制器，判断新数据包是否应该开启新的CPI
        SlotWriter slot_writer_;                         ///< 时隙写入器，负责将IQ数据写入CPI的正确位置
        ProgressTracker progress_tracker_;               ///< 进度跟踪器，监控CPI中各个PRT和通道的接收进度
        CpiFinalizer finalizer_;                         ///< CPI完成器，负责生成最终的CPI输出结果
        ProtocolSpec spec_{};                            ///< 协议规范，定义数据包格式和布局规则
        ControlSnapshot current_control_{};              ///< 当前暂存的控制快照，用于绑定或收敛活动 CPI
        SpscRing<CpiOutput> *output_ring_ = nullptr;     ///< 指向输出环形缓冲区的指针，用于发送完成的CPI结果
        SpscRing<ReleaseToken> *recycle_ring_ = nullptr; ///< 指向回收环形缓冲区的指针，用于释放CPI上下文资源
        std::uint64_t next_output_id_ = 1U;              ///< 下一个输出ID，用于唯一标识每个CPI输出
        std::uint32_t active_ctx_index_ =
            kInvalidPoolIndex;             ///< 活动CPI上下文在池中的索引，kInvalidPoolIndex表示无活动CPI
        CpiContext *active_ctx_ = nullptr; ///< 指向活动CPI上下文的指针
        std::uint32_t previous_ctx_index_ = kInvalidPoolIndex;            ///< 前一个CPI上下文在池中的索引，用于延迟清理
        CpiContext *previous_ctx_ = nullptr;                              ///< 指向前一个CPI上下文的指针
        OutputDropPolicy output_drop_policy_ = OutputDropPolicy::degrade; ///< 输出丢弃策略
        std::atomic<bool> output_degraded_{false};                        ///< 是否已发生输出退化
    };

} // namespace rxtech
