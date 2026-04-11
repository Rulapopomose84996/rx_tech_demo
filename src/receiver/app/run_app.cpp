#include "run_app.h"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>

#include "cli_args.h"
#include "rxtech/metrics.h"
#include "rxtech/receive_context.h"
#include "rxtech/receive_runner.h"
#include "rxtech/rx_config.h"
#include "path_utils.h"

#include "dpdk_backend.h"
#include "linux_socket_backend.h"
#include "../sidecar/internal/structured_logger.h"

namespace rxtech
{

    namespace
    {

        /**
         * 创建后端实例
         *
         * 根据指定的后端名称创建对应的后端对象。当前支持 dpdk 和 socket 两种后端类型。
         *
         * @param backend_name 后端类型名称，如 "dpdk" 或 "socket"
         * @return BackendPtr 指向创建的后端对象的智能指针
         * @throws std::runtime_error 当传入的后端类型不支持时抛出异常
         */
        BackendPtr make_backend(const std::string &backend_name)
        {
            if (backend_name == "dpdk")
            {
                return std::make_unique<DpdkIngress>();
            }
            if (backend_name == "socket")
            {
                return std::make_unique<LinuxSocketIngress>();
            }

            throw std::runtime_error("未知的后端类型: " + backend_name + "（当前支持 dpdk、socket）");
        }

        void print_usage(std::ostream &out, const char *program_name)
        {
            out << "用法: " << program_name << " --config FILE [选项]" << std::endl;
            out << "  --config FILE            指定配置文件" << std::endl;
            out << "  --dry-run                只解析配置并打印生效参数" << std::endl;
            out << "  --run-until-stopped      持续接收，直到收到 SIGINT 或 SIGTERM" << std::endl;
            out << "  --duration SECONDS       覆盖配置中的 duration_seconds" << std::endl;
            out << "  --status-interval SEC    覆盖配置中的 status_interval_seconds" << std::endl;
            out << "  -h, --help               显示帮助" << std::endl;
        }

        std::string effective_capture_output_dir(const RxConfig &config)
        {
            return config.capture.capture_output_dir.empty() ? config.operations.output_dir
                                                             : config.capture.capture_output_dir;
        }

        RxConfig build_effective_config(const std::string &backend_name, const CliArgs &args)
        {
            RxConfig effective = load_default_config();
            if (!args.config_path.empty())
            {
                effective = load_config_file(args.config_path);
            }
            effective.process.backend_name = backend_name;
            if (args.run_until_stopped)
            {
                effective.runtime.run_until_stopped = true;
            }
            if (args.duration_seconds.has_value())
            {
                effective.runtime.duration_seconds = *args.duration_seconds;
            }
            if (args.status_interval_seconds.has_value())
            {
                effective.operations.status_interval_seconds = *args.status_interval_seconds;
            }
            return effective;
        }

        /**
         * @brief 以 dry-run 模式打印配置信息
         *
         * 该函数用于在 dry-run 模式下输出所有生效的配置参数，便于用户验证配置是否正确解析。
         * 输出内容包括后端类型、捕获配置、原始记录配置、网络配置、协议配置和日志配置等。
         *
         * @param config RxConfig 配置对象，包含所有需要打印的配置项
         */
        void print_dry_run(const RxConfig &config)
        {
            std::cout << "[dry-run]" << std::endl;
            std::cout << "backend=" << config.process.backend_name << std::endl;
            std::cout << "config_path=" << config.process.config_path << std::endl;
            std::cout << "capture_enabled=" << (config.capture.capture_enabled ? "true" : "false") << std::endl;
            std::cout << "capture_output_dir=" << effective_capture_output_dir(config) << std::endl;
            std::cout << "capture_index_filename=" << config.capture.capture_index_filename << std::endl;
            std::cout << "capture_data_filename=" << config.capture.capture_data_filename << std::endl;
            std::cout << "raw_record_enabled=" << (config.capture.raw_record_enabled ? "true" : "false") << std::endl;
            std::cout << "raw_record_output_dir=" << config.capture.raw_record_output_dir << std::endl;
            std::cout << "raw_record_file_prefix=" << config.capture.raw_record_file_prefix << std::endl;
            std::cout << "raw_record_ring_slots=" << config.capture.raw_record_ring_slots << std::endl;
            std::cout << "raw_record_writer_batch_size=" << config.capture.raw_record_writer_batch_size << std::endl;
            std::cout << "raw_record_max_frame_bytes=" << config.capture.raw_record_max_frame_bytes << std::endl;
            std::cout << "raw_record_segment_bytes=" << config.capture.raw_record_segment_bytes << std::endl;
            std::cout << "raw_record_max_total_bytes=" << config.capture.raw_record_max_total_bytes << std::endl;
            std::cout << "interface=" << config.ingress.interface_name << std::endl;
            std::cout << "receiver_ipv4=" << config.ingress.receiver_ipv4 << std::endl;
            std::cout << "allowed_source_ipv4=" << config.ingress.allowed_source_ipv4 << std::endl;
            std::cout << "allowed_dest_port=" << config.ingress.allowed_dest_port << std::endl;
            std::cout << "socket_bind_ip=" << effective_socket_bind_ip(config) << std::endl;
            std::cout << "socket_bind_port=" << effective_socket_bind_port(config) << std::endl;
            std::cout << "socket_rcvbuf_bytes=" << config.ingress.socket_rcvbuf_bytes << std::endl;
            std::cout << "socket_nonblocking=" << (config.ingress.socket_nonblocking ? "true" : "false") << std::endl;
            std::cout << "socket_batch_timeout_ms=" << config.ingress.socket_batch_timeout_ms << std::endl;
            std::cout << "queue_id=" << config.ingress.queue_id << std::endl;
            std::cout << "duration_seconds=" << config.runtime.duration_seconds << std::endl;
            std::cout << "max_burst=" << config.runtime.max_burst << std::endl;
            std::cout << "packet_size_bytes=" << config.runtime.packet_size_bytes << std::endl;
            std::cout << "protocol_udp_packet_size=" << config.protocol.udp_packet_size << std::endl;
            std::cout << "protocol_channels_per_prt=" << config.protocol.channels_per_prt << std::endl;
            std::cout << "protocol_packets_per_channel=" << config.protocol.packets_per_channel << std::endl;
            std::cout << "run_until_stopped=" << (config.runtime.run_until_stopped ? "true" : "false") << std::endl;
            std::cout << "status_interval_seconds=" << config.operations.status_interval_seconds << std::endl;
            std::cout << "output_drop_policy=" << output_drop_policy_name(config.operations.output_drop_policy)
                      << std::endl;
            std::cout << "log_level=" << config.operations.log_level << std::endl;
            std::cout << "log_output=" << config.operations.log_output << std::endl;
            std::cout << "log_file_path=" << config.operations.log_file_path << std::endl;
            std::cout << "structured_log_output=" << config.operations.structured_log_output << std::endl;
            std::cout << "structured_log_file_path=" << config.operations.structured_log_file_path << std::endl;
            std::cout << "structured_log_format=" << config.operations.structured_log_format << std::endl;
            std::cout << "log_rate_limit_seconds=" << config.operations.log_rate_limit_seconds << std::endl;
            std::cout << "metrics_export_mode=" << config.operations.metrics_export_mode << std::endl;
            std::cout << "metrics_export_path=" << config.operations.metrics_export_path << std::endl;
            std::cout << "metrics_export_interval_seconds=" << config.operations.metrics_export_interval_seconds
                      << std::endl;
        }

    } // namespace

    /**
     * 运行接收器应用程序的主入口函数
     *
     * 该函数负责解析命令行参数、验证配置、初始化后端和指标收集器，
     * 并执行数据接收任务。支持干运行模式、日志输出到文件或控制台，
     * 以及持续运行直到手动停止的模式。
     *
     * @param backend_name 后端名称，用于创建相应的接收器后端实例
     * @param argc 命令行参数数量
     * @param argv 命令行参数数组
     * @return int 程序退出码：0表示成功，1表示一般错误，2表示资源不可用
     */
    int run_app(const std::string &backend_name, int argc, char **argv)
    {
        try
        {
            // 解析命令行参数并进行初步验证
            const CliArgs args = parse_cli_args(argc, argv);
            const char *program_name = argc > 0 ? argv[0] : "rx_receiver_dpdk";

            // 处理参数无效的情况
            if (!args.valid)
            {
                if (!args.error_message.empty())
                {
                    std::cerr << args.error_message << std::endl;
                }
                print_usage(std::cerr, program_name);
                return 1;
            }

            // 处理帮助请求
            if (args.help)
            {
                print_usage(std::cout, program_name);
                return 0;
            }

            // 验证必需的配置参数
            if (args.config_path.empty() && !args.dry_run)
            {
                std::cerr << "缺少必需参数: --config" << std::endl;
                print_usage(std::cerr, program_name);
                return 1;
            }

            // 构建接收上下文并生成有效配置
            ReceiveContext context;
            context.config = build_effective_config(backend_name, args);
            prepare_run_artifact_paths(context.config);

            // 验证配置的合法性
            const std::vector<std::string> validation_errors = validate_config(context.config);
            if (!validation_errors.empty())
            {
                for (const std::string &validation_error : validation_errors)
                {
                    std::cerr << "配置无效: " << validation_error << std::endl;
                }
                return 1;
            }

            // 处理干运行模式：仅打印配置信息而不实际执行
            if (args.dry_run)
            {
                print_dry_run(context.config);
                return 0;
            }

            configure_structured_logger(context.config);
            struct StructuredLoggerShutdownGuard
            {
                ~StructuredLoggerShutdownGuard()
                {
                    shutdown_structured_logger();
                }
            } structured_logger_shutdown_guard;

            structured_log(StructuredLogLevel::info, "receiver_start",
                           {{"backend", context.config.process.backend_name},
                            {"config_path", context.config.process.config_path},
                            {"structured_log_backend", structured_logger_backend_name()}});

            // 初始化后端实例和指标收集器
            context.backend = make_backend(backend_name);
            context.metrics = std::make_unique<MetricsCollector>();

            // 配置日志输出目标
            ReceiveRunner runner;
            std::ofstream log_stream;
            std::ostream *status_output = nullptr;
            if (context.config.operations.log_output == "file" && !context.config.operations.log_file_path.empty())
            {
                path_utils::ensure_parent_directory(context.config.operations.log_file_path);
                log_stream.open(context.config.operations.log_file_path, std::ios::out | std::ios::trunc);
                if (!log_stream.is_open())
                {
                    throw std::runtime_error("无法打开日志文件: " + context.config.operations.log_file_path);
                }
                status_output = &log_stream;
            }
            else if (context.config.runtime.run_until_stopped)
            {
                status_output = &std::cout;
            }
            if (status_output != nullptr)
            {
                runner.set_status_output(status_output);
            }

            // 执行接收任务并获取运行摘要
            RunSummary summary = runner.run(context);
            summary.run.structured_log_backend = structured_logger_backend_name();
            structured_log(StructuredLogLevel::info, "receiver_stop",
                           {{"status", summary.run.status},
                            {"backend", summary.run.backend_name},
                            {"structured_log_backend", summary.run.structured_log_backend}});
            if (!summary.run.human_summary.empty())
            {
                if (log_stream.is_open())
                {
                    log_stream << summary.run.human_summary;
                    log_stream.flush();
                    std::cout << summary.run.human_summary;
                    std::cout << "日志文件: " << context.config.operations.log_file_path << std::endl;
                }
                else
                {
                    std::cout << summary.run.human_summary;
                }
            }
            else
            {
                std::cout << "运行结果: " << (summary.run.status == "success" ? "成功" : "失败") << std::endl;
            }

            // 处理运行失败的情况
            if (summary.run.status != "success")
            {
                std::cerr << "运行失败: " << summary.run.error_message << std::endl;
                return summary.run.status == "unavailable" ? 2 : 1;
            }
        }
        catch (const std::exception &ex)
        {
            structured_log(StructuredLogLevel::error, "receiver_exception", {{"message", ex.what()}});
            std::cerr << "运行失败: " << ex.what() << std::endl;
            return 1;
        }

        return 0;
    }

} // namespace rxtech
