/**
 * @file owner_loop.cpp
 * @brief OwnerLoop 类的实现文件
 * 
 * 实现了数据包捕获系统的核心主循环逻辑，包括：
 * - 数据包接收和处理
 * - CPI（Capture Packet Information）状态管理
 * - 数据顺序跟踪
 * - 异步输出处理
 * - 运行时状态报告
 */

#include "rxtech/owner_loop.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ostream>
#include <thread>

#include "internal/cpi_state_coordinator.h"
#include "internal/owner_loop_runtime_state.h"
#include "data_order_tracker.h"
#include "udp_datagram_pipeline.h"
#include "rxtech/cpi_consumer.h"
#include "rxtech/raw_frame_recorder.h"
#include "rxtech/time_utils.h"
#include "runtime_status_reporter.h"

namespace rxtech
{

    /**
     * @brief 设置状态输出流
     *
     * 配置运行时状态信息的输出目标，用于周期性输出接收状态和诊断信息。
     *
     * @param output 指向输出流的指针，可以为nullptr表示禁用状态输出
     */
    void OwnerLoop::set_status_output(std::ostream *output)
    {
        status_output_ = output;
    }

    /**
     * @brief 执行数据包接收和处理的主循环
     *
     * 这是接收系统的核心函数，负责从后端接收数据包 burst，经过协议解析、CPI状态管理、
     * 数据顺序跟踪等处理后，将有效数据写入输出流。函数采用生产者-消费者模式，通过
     * CPI consumer 线程异步处理 CPI 输出。
     *
     * 主要处理流程：
     * 1. 初始化协议规范、包管道、CPI状态协调器和数据顺序跟踪器
     * 2. 启动 CPI consumer 线程处理异步输出
     * 3. 循环接收数据包 burst，对每个包进行：
     *    - 原始帧录制（如果启用）
     *    - 协议解析和验证
     *    - 控制表包处理
     *    - 数据包处理和 CPI 状态更新
     *    - 数据写入和统计更新
     * 4. 定期检查超时和回收资源
     * 5. 优雅关闭：停止 consumer 线程、刷新流、生成最终摘要
     *
     * @param context 接收上下文，包含后端接口、配置信息和性能指标
     * @param artifacts 捕获产物，包含输出流、录制器和统计计数器
     * @param should_stop 停止检查函数，返回true时终止循环
     * @return RunSummary 运行摘要，包含接收统计、错误信息和性能指标
     */
    RunSummary OwnerLoop::run(ReceiveContext &context,
                              CaptureArtifacts &artifacts,
                              const std::function<bool()> &should_stop) const
    {
        // 从配置中构建协议规范
        const ProtocolSpec spec = protocol_spec_from_config(context.config);
        
        // 初始化数据包处理管道
        UdpDatagramPipeline packet_pipeline(context.config, spec);
        
        // 初始化 CPI 状态协调器，管理 CPI 生命周期
        CpiStateCoordinator cpi_state_coordinator(spec);
        
        // 初始化数据顺序跟踪器，检测数据包乱序
        DataOrderTracker data_order_tracker(spec);

        // 写入索引文件的 CSV 表头
        *artifacts.index_stream << "cpi,channel,prt,packet_index,packet_kind,payload_len,valid\n";

        // CPI 输出管道：owner → output_ring → consumer → recycle_ring → owner
        // 使用无锁单生产者单消费者环形缓冲区实现线程间通信
        constexpr std::size_t kOutputRingCapacity = 32U;
        SpscRing<CpiOutput> output_ring(kOutputRingCapacity);
        SpscRing<ReleaseToken> recycle_ring(kOutputRingCapacity);
        cpi_state_coordinator.attach_rings(&output_ring, &recycle_ring);

        // 启动 CPI consumer 线程，异步处理 CPI 输出
        std::atomic<bool> consumer_stop{false};
        CpiConsumer consumer(output_ring, recycle_ring, output_handler_);
        std::thread consumer_thread([&]
                                    { consumer.run(consumer_stop); });

        // 记录开始时间，用于计算运行时长
        const auto start_time = std::chrono::steady_clock::now();
        
        // 初始化运行时状态报告器，定期输出状态信息
        RuntimeStatusReporter status_reporter(context.config, spec, status_output_, start_time);

        // 初始化运行时状态和无效包计数器
        OwnerLoopRuntimeState runtime_state;
        std::uint32_t invalid_dumped = 0;

        // 主循环：持续接收和处理数据包，直到收到停止信号
        while (!should_stop())
        {
            // 从后端接收数据包 burst
            UdpDatagramBurst burst;
            if (!context.backend->recv_burst(burst, context.config.max_burst))
            {
                runtime_state.run_status = "error";
                runtime_state.run_error = "backend recv_burst failed";
                context.backend->release_burst(burst);
                break;
            }

            struct BurstReleaseGuard
            {
                IRxBackend *backend = nullptr;
                UdpDatagramBurst *burst = nullptr;
                ~BurstReleaseGuard()
                {
                    if (backend != nullptr && burst != nullptr)
                    {
                        backend->release_burst(*burst);
                    }
                }
            } burst_release_guard{context.backend.get(), &burst};

            std::uint64_t burst_bytes = 0;
            std::size_t accepted_packets = 0U;
            
            // 处理 burst 中的每个数据包
            for (const UdpDatagramDesc &datagram : burst.datagrams)
            {
                // Drain recycle tokens before processing the next datagram so
                // large bursts do not temporarily exhaust the CPI context pool
                // after several back-to-back finalizations.
                cpi_state_coordinator.drain_recycle(*context.metrics);

                // 如果启用了原始帧录制，提交数据包到录制器
                if (artifacts.raw_frame_recorder != nullptr &&
                    datagram.raw_frame_data != nullptr &&
                    datagram.raw_frame_len != 0U)
                {
                    PacketDesc packet;
                    packet.data = const_cast<std::uint8_t *>(datagram.raw_frame_data);
                    packet.len = datagram.raw_frame_len;
                    packet.ts_ns = datagram.ts_ns;
                    packet.queue_id = datagram.queue_id;
                    packet.cookie = datagram.cookie;
                    artifacts.raw_frame_recorder->submit(packet);
                }

                // 通过包管道处理数据包，包括协议解析、验证和 CPI 状态更新
                const PacketProcessStats process_stats = packet_pipeline.process_datagram(
                    datagram,
                    *context.metrics,
                    status_reporter.diagnostic_output(),
                    invalid_dumped,
                    [&](const ProcessedPacket &processed)
                    {
                        // 记录协议层数据包统计
                        runtime_state.record_protocol_packet(processed.interpreted);
                        
                        // 处理控制表包：更新 CPI 状态协调器
                        if (processed.interpreted.kind == PacketKind::control_table)
                        {
                            cpi_state_coordinator.process_control_packet(processed.parsed);
                        }
                        
                        // 处理数据包：验证顺序、更新 CPI 状态、写入输出
                        if (processed.interpreted.kind == PacketKind::data_packet)
                        {
                            // 观察数据包顺序，检测乱序
                            data_order_tracker.observe(processed.interpreted);
                            
                            // 处理数据包并获取 CPI 处理结果
                            const CpiProcessResult cpi_result = cpi_state_coordinator.process_data_packet(processed.parsed,
                                                                                                          processed.interpreted,
                                                                                                          spec,
                                                                                                          *context.metrics,
                                                                                                          runtime_state.run_status,
                                                                                                          runtime_state.run_error);
                            
                            // 如果 CPI 不接受该数据包，跳过后续处理
                            if (!cpi_result.accepted)
                            {
                                return;
                            }
                            
                            // 记录已接受的数据包统计
                            runtime_state.record_data_packet(processed.parsed, processed.interpreted, spec);
                        }

                        // 记录已捕获的数据包统计
                        runtime_state.record_captured_packet(processed.interpreted);
                        
                        // 将 UDP 载荷写入数据包输出流
                        artifacts.packet_stream->write(
                            reinterpret_cast<const char *>(processed.udp_frame.udp_payload.data()),
                            static_cast<std::streamsize>(processed.udp_frame.udp_payload.size()));
                        
                        // 将数据包元数据写入索引流（CSV 格式）
                        *artifacts.index_stream
                            << processed.interpreted.cpi
                            << ',' << processed.interpreted.channel
                            << ',' << processed.interpreted.prt
                            << ',' << processed.interpreted.packet_index
                            << ',' << packet_kind_name(processed.interpreted.kind)
                            << ',' << processed.udp_frame.udp_payload.size()
                            << ',' << (processed.interpreted.valid ? "true" : "false")
                            << '\n';
                        
                        // 更新捕获统计信息
                        artifacts.file_offset += processed.udp_frame.udp_payload.size();
                        artifacts.recorded_bytes += processed.udp_frame.udp_payload.size();
                        ++artifacts.recorded_packets;
                        artifacts.captured_bytes += processed.udp_frame.udp_payload.size();
                        ++artifacts.captured_packets;
                    });

                if (runtime_state.run_status != "success")
                {
                    break;
                }

                // 累积 burst 统计信息
                burst_bytes += process_stats.accepted_bytes;
                accepted_packets += process_stats.accepted_packets;
                runtime_state.filtered_packets += process_stats.filtered_packets;
            }

            if (runtime_state.run_status != "success")
            {
                break;
            }

            // 如果有接受的数据包，更新性能指标
            if (accepted_packets > 0U)
            {
                context.metrics->on_burst(accepted_packets, burst_bytes);
            }
            
            // T-004: 定期检查活跃 CPI 的超时状态
            cpi_state_coordinator.check_timeout(steady_clock_now_ns(), *context.metrics);

            // 从 consumer 线程回收已处理的 CPI 资源
            cpi_state_coordinator.drain_recycle(*context.metrics);

            // 定期输出运行时状态报告
            status_reporter.emit_periodic(context,
                                          artifacts,
                                          runtime_state,
                                          data_order_tracker,
                                          std::chrono::steady_clock::now());
        }

        // 刷新所有输出流，确保数据完全写入
        artifacts.packet_stream->flush();
        artifacts.index_stream->flush();

        // 关闭流程：
        // 1. 接收循环已停止（上述 while 循环退出）
        // 2.  finalize 任何活跃或之前的 CPI 状态
        cpi_state_coordinator.finalize_active_for_shutdown(*context.metrics);

        // 3-4. 通知 consumer 线程退出并等待其完成
        consumer_stop.store(true, std::memory_order_release);
        consumer_thread.join();

        // 5. 清空回收环，释放所有待回收的资源
        cpi_state_coordinator.drain_recycle(*context.metrics);

        // 6. 停止原始帧录制器（如果正在运行）
        if (artifacts.raw_frame_recorder != nullptr)
        {
            artifacts.raw_frame_recorder->stop();
        }

        // 记录结束时间并生成最终摘要
        const auto end_time = std::chrono::steady_clock::now();
        RunSummary summary = status_reporter.build_final_summary(context,
                                                                 artifacts,
                                                                 runtime_state,
                                                                 data_order_tracker,
                                                                 end_time);
        
        // 渲染并输出最终摘要
        status_reporter.render_final(summary, end_time - start_time);
        
        return summary;
    }

} // namespace rxtech
