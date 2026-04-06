#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>

#include "rxtech/cpi_consumer.h"
#include "rxtech/receive_context.h"

/**
 * @namespace rxtech
 * @brief RX Tech 核心技术命名空间
 * 
 * 包含数据包捕获、处理和记录相关的核心类和工具函数。
 */
namespace rxtech
{

    class RawFrameRecorder;

    /**
     * @brief 构建运行摘要的人类可读字符串表示
     * 
     * 将 RunSummary 结构体转换为易于阅读的文本格式，用于状态输出和日志记录。
     * 
     * @param summary 运行摘要结构体，包含捕获过程的统计信息
     * @return std::string 格式化后的人类可读摘要字符串
     */
    std::string build_run_human_summary(const RunSummary &summary);

    /**
     * @brief 捕获产物结构体，用于存储数据包捕获过程中的输出流和统计信息
     * 
     * 该结构体封装了数据包捕获过程中所需的各种输出目标和统计数据，
     * 包括数据包流、索引流、原始帧记录器以及相关的计数信息。
     */
    struct CaptureArtifacts
    {
        std::ostream *packet_stream = nullptr;             ///< 数据包输出流，用于写入捕获的数据包内容
        std::ostream *index_stream = nullptr;              ///< 索引输出流，用于写入数据包索引信息
        RawFrameRecorder *raw_frame_recorder = nullptr;    ///< 原始帧记录器，用于记录原始帧数据
        std::uint64_t file_offset = 0;                     ///< 文件偏移量，记录当前写入位置
        std::uint64_t recorded_packets = 0;                ///< 已记录的数据包数量
        std::uint64_t recorded_bytes = 0;                  ///< 已记录的字节数
        std::uint64_t captured_packets = 0;                ///< 已捕获的数据包数量
        std::uint64_t captured_bytes = 0;                  ///< 已捕获的字节数
    };

    /**
     * @brief 所有者循环类，负责管理数据包捕获的主循环逻辑
     * 
     * OwnerLoop 是数据包捕获系统的核心控制类，负责：
     * - 配置状态输出流
     * - 设置 CPI 输出处理器
     * - 执行捕获主循环，处理接收上下文中的数据
     * 
     * 该类采用分离关注点设计，将捕获逻辑与具体的输出处理解耦，
     * 通过回调机制实现灵活的数据处理策略。
     */
    class OwnerLoop
    {
    public:
        /**
         * @brief 设置状态输出流
         * 
         * 配置用于输出运行时状态信息的流对象，如进度、统计信息等。
         * 
         * @param output 指向输出流的指针，可为 nullptr 表示禁用状态输出
         */
        void set_status_output(std::ostream *output);

        /**
         * @brief 设置 CPI 输出处理器
         * 
         * 注册一个回调函数，用于处理捕获过程中的 CPI（Capture Packet Information）数据。
         * 该处理器将在每个数据包被捕获时调用，允许自定义数据处理逻辑。
         * 
         * @param handler CPI 输出处理函数对象，接收捕获的数据包信息作为参数
         */
        void set_output_handler(CpiOutputHandler handler) { output_handler_ = std::move(handler); }

        /**
         * @brief 执行数据包捕获主循环
         * 
         * 启动并运行数据包捕获循环，从接收上下文中读取数据，
         * 将其写入捕获产物（CaptureArtifacts），并通过输出处理器进行处理。
         * 
         * 该函数会持续运行直到满足以下任一条件：
         * - should_stop 回调返回 true
         * - 接收上下文中的数据已全部处理完毕
         * - 发生错误
         * 
         * @param context 接收上下文，提供待处理的数据源和元数据
         * @param artifacts 捕获产物，用于存储输出流和统计信息
         * @param should_stop 停止检查回调函数，返回 true 时表示应终止捕获循环
         * @return RunSummary 运行摘要，包含捕获过程的详细统计信息
         */
        RunSummary run(ReceiveContext &context,
                       CaptureArtifacts &artifacts,
                       const std::function<bool()> &should_stop) const;

    private:
        std::ostream *status_output_ = nullptr;    ///< 状态输出流指针，用于输出运行时状态信息
        CpiOutputHandler output_handler_;          ///< CPI 输出处理器，用于处理捕获的数据包信息
    };

} // namespace rxtech
