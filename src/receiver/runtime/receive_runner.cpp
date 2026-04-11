#include "rxtech/receive_runner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>

#include "rxtech/owner_loop.h"
#include "rxtech/raw_frame_recorder.h"
#include "internal/debug_capture_writer.h"
#include "../sidecar/internal/run_context_snapshot.h"
#include "../sidecar/internal/summary_renderer.h"
#include "internal/path_utils.h"

namespace rxtech
{

    namespace
    {

        std::atomic<ReceiveRunner *> g_active_receive_runner{nullptr};

        class SignalHandlerGuard
        {
          public:
            explicit SignalHandlerGuard(ReceiveRunner &runner)
                : previous_runner_(g_active_receive_runner.exchange(&runner))
            {
                previous_sigint_ = std::signal(SIGINT, signal_handler);
                previous_sigterm_ = std::signal(SIGTERM, signal_handler);
            }

            ~SignalHandlerGuard()
            {
                std::signal(SIGINT, previous_sigint_);
                std::signal(SIGTERM, previous_sigterm_);
                g_active_receive_runner.store(previous_runner_);
            }

          private:
            using SignalHandler = void (*)(int);

            static void signal_handler(int)
            {
                ReceiveRunner *runner = g_active_receive_runner.load();
                if (runner != nullptr)
                {
                    runner->request_stop();
                }
            }

            SignalHandler previous_sigint_ = SIG_DFL;
            SignalHandler previous_sigterm_ = SIG_DFL;
            ReceiveRunner *previous_runner_ = nullptr;
        };

        void close_stream_or_throw(std::ofstream &stream, const std::string &path, const char *label)
        {
            if (!stream.is_open())
            {
                return;
            }

            stream.flush();
            const bool flush_failed = !stream.good();
            stream.close();
            const bool close_failed = stream.fail();
            stream.clear();
            if (flush_failed || close_failed)
            {
                throw std::runtime_error(std::string("关闭") + label + "失败: " + path);
            }
        }

        std::string sanitize_run_label(std::string label)
        {
            for (char &ch : label)
            {
                const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                                  ch == '_' || ch == '-';
                if (!keep)
                {
                    ch = '_';
                }
            }
            while (!label.empty() && (label.front() == '_' || label.front() == '-'))
            {
                label.erase(label.begin());
            }
            return label.empty() ? std::string{"run"} : label;
        }

        std::string make_run_timestamp()
        {
            const std::time_t now = std::time(nullptr);
            std::tm local_time{};
#ifdef _WIN32
            localtime_s(&local_time, &now);
#else
            localtime_r(&now, &local_time);
#endif
            char buffer[32] = {};
            if (std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local_time) == 0U)
            {
                throw std::runtime_error("运行时间戳格式化失败");
            }
            return buffer;
        }

        std::string choose_run_suffix(const RxConfig &config)
        {
            const std::string capture_dir = config.capture.capture_output_dir.empty()
                                                ? config.operations.output_dir
                                                : config.capture.capture_output_dir;
            std::string suffix = sanitize_run_label(path_utils::path_filename(capture_dir));
            if (suffix == "run" && !config.process.backend_name.empty())
            {
                suffix = sanitize_run_label(config.process.backend_name);
            }
            return suffix;
        }

        std::string make_capture_run_dir(const std::string &capture_dir, const std::string &run_label)
        {
            const std::string parent = path_utils::path_parent(capture_dir);
            return path_utils::join_path(parent, run_label);
        }

        std::string make_raw_record_run_dir(const std::string &raw_record_dir, const std::string &run_label)
        {
            return path_utils::join_path(raw_record_dir, run_label);
        }

        std::string make_log_run_path(const std::string &log_file_path, const std::string &run_label)
        {
            const std::string parent = path_utils::path_parent(log_file_path);
            const std::string filename = path_utils::path_filename(log_file_path);
            return path_utils::join_path(path_utils::join_path(parent, run_label), filename);
        }

        /**
         * 创建后端不可用时的运行摘要信息
         *
         * 当后端初始化失败或后端不可用时，构建包含错误信息的运行摘要对象。
         * 该摘要用于记录后端状态、错误原因以及基本的运行时统计信息。
         *
         * @param context 接收上下文，包含后端实例和配置信息
         * @param init_result 后端初始化结果，包含可用性和错误原因等信息
         * @return RunSummary 填充了后端不可用状态的运行摘要对象
         */
        RunSummary make_unavailable_summary(const ReceiveContext &context, const BackendInitResult &init_result)
        {
            RunSummary summary;
            summary.run.backend_name =
                context.backend != nullptr ? context.backend->name() : context.config.process.backend_name;
            summary.run.status = init_result.available ? "error" : "unavailable";
            summary.run.error_message = init_result.reason;
            summary.backend.available = init_result.available;
            summary.backend.status = init_result.available ? "available" : "unavailable";
            summary.backend.reason = init_result.reason;
            return summary;
        }

    } // namespace

    void request_receive_stop()
    {
        ReceiveRunner *runner = g_active_receive_runner.load();
        if (runner != nullptr)
        {
            runner->request_stop();
        }
    }

    void reset_receive_stop()
    {
        ReceiveRunner *runner = g_active_receive_runner.load();
        if (runner != nullptr)
        {
            runner->reset_stop();
        }
    }

    void ReceiveRunner::request_stop() noexcept
    {
        stop_requested_.store(true);
    }

    void ReceiveRunner::reset_stop() noexcept
    {
        stop_requested_.store(false);
    }

    bool ReceiveRunner::stop_requested() const noexcept
    {
        return stop_requested_.load();
    }

    /**
     * 准备运行产物路径配置
     *
     * 根据配置文件中的设置，生成并更新本次运行的各种输出路径，包括捕获数据目录、
     * 原始记录目录和日志文件路径。所有路径都会附加运行标签以确保唯一性。
     * 如果路径已经准备过，则直接返回避免重复处理。
     *
     * @param config 接收器配置对象的引用，包含所有路径配置和运行状态信息
     *               - run_artifacts_prepared: 标记是否已准备过运行产物路径
     *               - run_label: 输出的运行标签，由时间戳和后缀组成
     *               - output_dir: 主输出目录，会被更新为带运行标签的路径
     *               - capture_output_dir: 捕获数据输出目录，会被更新为带运行标签的路径
     *               - raw_record_enabled: 是否启用原始记录功能
     *               - raw_record_output_dir: 原始记录输出目录，会被更新为带运行标签的路径
     *               - log_output: 日志输出方式（"file"表示文件输出）
     *               - log_file_path: 日志文件路径，会被更新为带运行标签的路径
     */
    void prepare_run_artifact_paths(RxConfig &config)
    {
        // 如果路径已准备，直接返回避免重复处理
        if (config.operations.run_artifacts_prepared)
        {
            return;
        }

        // 生成运行标签：时间戳 + 后缀
        const std::string run_suffix = choose_run_suffix(config);
        config.process.run_label = make_run_timestamp() + "_" + run_suffix;

        // 处理捕获数据输出目录
        const std::string capture_dir = config.capture.capture_output_dir.empty() ? config.operations.output_dir
                                                                                  : config.capture.capture_output_dir;
        if (!capture_dir.empty())
        {
            const std::string run_capture_dir = make_capture_run_dir(capture_dir, config.process.run_label);
            config.operations.output_dir = run_capture_dir;
            config.capture.capture_output_dir = run_capture_dir;
        }

        // 处理原始记录输出目录
        if (config.capture.raw_record_enabled && !config.capture.raw_record_output_dir.empty())
        {
            config.capture.raw_record_output_dir =
                make_raw_record_run_dir(config.capture.raw_record_output_dir, config.process.run_label);
        }

        // 处理日志文件路径
        if (config.operations.log_output == "file" && !config.operations.log_file_path.empty())
        {
            config.operations.log_file_path =
                make_log_run_path(config.operations.log_file_path, config.process.run_label);
        }

        // 标记路径已准备完成
        config.operations.run_artifacts_prepared = true;
    }

    void ReceiveRunner::set_status_output(std::ostream *output)
    {
        status_output_ = output;
    }

    /**
     * 执行数据接收运行的主函数
     *
     * 该函数负责初始化后端、配置捕获和录制功能，并执行主要的数据接收循环。
     * 它会处理信号中断、管理资源生命周期，并返回运行摘要信息。
     *
     * @param context 接收上下文，包含后端实例、配置信息和指标收集器
     *                - context.backend: 后端接口实例，用于实际的数据接收
     *                - context.metrics: 指标收集器，用于统计运行数据
     *                - context.config: 运行配置，包括捕获、录制等参数
     *
     * @return RunSummary 运行摘要对象，包含以下关键信息：
     *         - backend_available: 后端是否可用
     *         - backend_status: 后端状态描述
     *         - capture_packets_path: 捕获数据包文件路径
     *         - capture_index_path: 捕获索引文件路径
     *         - captured_packets: 捕获的数据包数量
     *         - captured_bytes: 捕获的字节数
     *         - recorded_packets: 记录的数据包数量
     *         - recorded_bytes: 记录的字节数
     *         - run_artifact_dir: 运行产物目录
     *         - raw_record_*: 原始帧录制相关的统计信息
     *         - human_summary: 人类可读的运行摘要文本
     *
     * @throws std::runtime_error 当上下文不完整或文件操作失败时抛出异常
     */
    RunSummary ReceiveRunner::run(ReceiveContext &context)
    {
        // 验证上下文的完整性
        if (!context.backend || !context.metrics)
        {
            throw std::runtime_error("接收上下文不完整");
        }

        // 重置停止标志并设置信号处理器
        reset_stop();
        SignalHandlerGuard signal_guard(*this);
        prepare_run_artifact_paths(context.config);

        // 初始化后端，如果失败则返回不可用摘要
        const BackendInitResult init_result = context.backend->init(context.config);
        if (!init_result.ok)
        {
            RunSummary summary = make_unavailable_summary(context, init_result);
            context.backend->shutdown();
            return summary;
        }

        try
        {
            // 配置数据捕获的文件路径和流
            const std::string output_dir = context.config.capture.capture_output_dir.empty()
                                               ? context.config.operations.output_dir
                                               : context.config.capture.capture_output_dir;
            const bool capture_enabled = context.config.capture.capture_enabled;
            const std::string capture_packets_path =
                capture_enabled ? (output_dir + "/" + context.config.capture.capture_data_filename) : std::string{};
            const std::string capture_index_path =
                capture_enabled ? (output_dir + "/" + context.config.capture.capture_index_filename) : std::string{};
            std::ofstream capture_packets_stream;
            std::ofstream capture_index_stream;
            std::ostringstream capture_packets_sink;
            std::ostringstream capture_index_sink;
            if (capture_enabled)
            {
                path_utils::ensure_parent_directory(capture_packets_path);
                path_utils::ensure_parent_directory(capture_index_path);

                capture_packets_stream.open(capture_packets_path, std::ios::binary | std::ios::trunc);
                capture_index_stream.open(capture_index_path, std::ios::trunc);
                if (!capture_packets_stream.is_open())
                {
                    throw std::runtime_error("打开 capture 文件失败: " + capture_packets_path);
                }
                if (!capture_index_stream.is_open())
                {
                    throw std::runtime_error("打开 capture 索引文件失败: " + capture_index_path);
                }
            }

            // 启动原始帧录制器（如果启用）
            RawFrameRecorder raw_frame_recorder(context.config);
            if (raw_frame_recorder.enabled())
            {
                raw_frame_recorder.start();
            }

            // 配置捕获产物并执行主接收循环
            OwnerLoop owner_loop;
            owner_loop.set_status_output(status_output_);
            CaptureArtifacts artifacts;
            DebugCaptureWriter capture_writer(context.config.capture.capture_policy,
                                              capture_enabled ? static_cast<std::ostream *>(&capture_packets_stream)
                                                              : static_cast<std::ostream *>(&capture_packets_sink),
                                              capture_enabled ? static_cast<std::ostream *>(&capture_index_stream)
                                                              : static_cast<std::ostream *>(&capture_index_sink),
                                              output_dir);
            artifacts.packet_stream = capture_enabled ? static_cast<std::ostream *>(&capture_packets_stream)
                                                      : static_cast<std::ostream *>(&capture_packets_sink);
            artifacts.index_stream = capture_enabled ? static_cast<std::ostream *>(&capture_index_stream)
                                                     : static_cast<std::ostream *>(&capture_index_sink);
            artifacts.capture_writer = &capture_writer;
            artifacts.raw_frame_recorder = raw_frame_recorder.enabled() ? &raw_frame_recorder : nullptr;

            const auto start_time = std::chrono::steady_clock::now();
            const auto deadline =
                start_time + std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.runtime.duration_seconds));
            RunSummary summary = owner_loop.run(context, artifacts,
                                                [&]()
                                                {
                                                    return context.config.runtime.run_until_stopped
                                                               ? stop_requested()
                                                               : (std::chrono::steady_clock::now() >= deadline);
                                                });

            // 停止录制器并检查错误
            raw_frame_recorder.stop();
            const std::string raw_record_error = raw_frame_recorder.error_message();
            if (!raw_record_error.empty())
            {
                throw std::runtime_error("原始帧录制失败: " + raw_record_error);
            }

            close_stream_or_throw(capture_packets_stream, capture_packets_path, " capture 文件");
            close_stream_or_throw(capture_index_stream, capture_index_path, " capture 索引文件");

            // 填充运行摘要信息
            const RawFrameRecorderStats raw_record_stats = raw_frame_recorder.snapshot();
            summary.backend.available = true;
            summary.backend.status = "available";
            summary.capture.capture_policy = capture_policy_name(context.config.capture.capture_policy);
            summary.capture.packets_path = capture_packets_path;
            summary.capture.index_path = capture_index_path;
            summary.capture.captured_packets = artifacts.captured_packets;
            summary.capture.captured_bytes = artifacts.captured_bytes;
            summary.capture.recorded_packets = artifacts.recorded_packets;
            summary.capture.recorded_bytes = artifacts.recorded_bytes;
            summary.capture.run_artifact_dir = output_dir;
            summary.capture.raw_record_output_dir =
                raw_frame_recorder.enabled() ? raw_frame_recorder.output_dir() : std::string{};
            summary.capture.raw_record_latest_file_path = raw_record_stats.latest_file_path;
            summary.capture.raw_record_written_frames = raw_record_stats.written_frames;
            summary.capture.raw_record_written_bytes = raw_record_stats.written_bytes;
            summary.capture.raw_record_dropped_frames = raw_record_stats.dropped_frames;
            summary.capture.raw_record_dropped_bytes = raw_record_stats.dropped_bytes;
            summary.capture.raw_record_retained_bytes = raw_record_stats.retained_bytes;
            summary.capture.raw_record_queue_high_watermark = raw_record_stats.queue_high_watermark;
            const RunHeaderSnapshot header = build_run_header_snapshot(context.config);
            summary.run.run_id = header.run_id;
            summary.run.config_path = header.config_path;
            summary.run.events_path = header.events_path;
            summary.run.summary_json_path = header.summary_json_path;
            summary.run.summary_text_path = header.summary_text_path;
            summary.run.human_summary = build_run_human_summary(summary);

            path_utils::ensure_parent_directory(summary.run.summary_json_path);
            path_utils::ensure_parent_directory(summary.run.summary_text_path);

            {
                std::ofstream summary_json_stream(summary.run.summary_json_path, std::ios::trunc);
                if (!summary_json_stream.is_open())
                {
                    throw std::runtime_error("打开 summary.json 失败: " + summary.run.summary_json_path);
                }
                summary_json_stream << render_summary_json(summary, header);
            }

            {
                std::ofstream summary_text_stream(summary.run.summary_text_path, std::ios::trunc);
                if (!summary_text_stream.is_open())
                {
                    throw std::runtime_error("打开 summary.txt 失败: " + summary.run.summary_text_path);
                }
                summary_text_stream << render_summary_text(summary, header);
            }

            context.backend->shutdown();
            return summary;
        }
        catch (...)
        {
            // 确保异常情况下也能正确关闭后端
            context.backend->shutdown();
            throw;
        }
    }

} // namespace rxtech
