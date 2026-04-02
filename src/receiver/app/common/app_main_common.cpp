#include "app_main_common.h"

#include <iostream>
#include <memory>
#include <stdexcept>

#include "cli_args.h"
#include "rxtech/metrics.h"
#include "rxtech/receive_context.h"
#include "rxtech/receive_runner.h"
#include "rxtech/rx_config.h"

#if defined(RXTECH_HAS_DPDK_BACKEND)
#include "dpdk_backend.h"
#endif

namespace rxtech
{

    namespace
    {

        class UnavailableBackend final : public IRxBackend
        {
        public:
            UnavailableBackend(std::string backend_name, std::string reason)
                : backend_name_(std::move(backend_name)), reason_(std::move(reason))
            {
            }

            std::string name() const override
            {
                return backend_name_;
            }

            BackendInitResult init(const RxConfig &) override
            {
                BackendInitResult result;
                result.available = false;
                result.reason = reason_;
                return result;
            }

            bool recv_burst(RxBurst &, std::uint32_t) override
            {
                return false;
            }

            void release_burst(RxBurst &burst) override
            {
                burst.packets.clear();
            }

            BackendStats stats() const override
            {
                return {};
            }

            void shutdown() override
            {
            }

        private:
            std::string backend_name_;
            std::string reason_;
        };

        BackendPtr make_backend(const std::string &backend_name)
        {
            if (backend_name == "dpdk")
            {
#if defined(RXTECH_HAS_DPDK_BACKEND)
                return std::make_unique<DpdkIngress>();
#else
                return std::make_unique<UnavailableBackend>("dpdk", "DPDK backend is disabled at build time");
#endif
            }

            throw std::runtime_error("unknown backend: " + backend_name + " (only dpdk is supported)");
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
            return config.capture_output_dir.empty() ? config.output_dir : config.capture_output_dir;
        }

        RxConfig build_effective_config(const std::string &backend_name, const CliArgs &args)
        {
            RxConfig effective = load_default_config();
            if (!args.config_path.empty())
            {
                effective = load_config_file(args.config_path);
            }
            effective.backend_name = backend_name;
            if (args.run_until_stopped)
            {
                effective.run_until_stopped = true;
            }
            if (args.duration_seconds.has_value())
            {
                effective.duration_seconds = *args.duration_seconds;
            }
            if (args.status_interval_seconds.has_value())
            {
                effective.status_interval_seconds = *args.status_interval_seconds;
            }
            return effective;
        }

        std::string validate_config(const RxConfig &config)
        {
            if (config.capture_enabled)
            {
                if (effective_capture_output_dir(config).empty())
                {
                    return "capture_output_dir must not be empty when capture is enabled";
                }
                if (config.capture_index_filename.empty())
                {
                    return "capture_index_filename must not be empty when capture is enabled";
                }
                if (config.capture_data_filename.empty())
                {
                    return "capture_data_filename must not be empty when capture is enabled";
                }
            }
            if (config.raw_record_enabled)
            {
                if (config.raw_record_output_dir.empty())
                {
                    return "raw_record_output_dir must not be empty when raw recording is enabled";
                }
                if (config.raw_record_file_prefix.empty())
                {
                    return "raw_record_file_prefix must not be empty when raw recording is enabled";
                }
                if (config.raw_record_ring_slots == 0U)
                {
                    return "raw_record_ring_slots must be greater than 0";
                }
                if (config.raw_record_writer_batch_size == 0U)
                {
                    return "raw_record_writer_batch_size must be greater than 0";
                }
                if (config.raw_record_max_frame_bytes == 0U)
                {
                    return "raw_record_max_frame_bytes must be greater than 0";
                }
                if (config.raw_record_segment_bytes == 0U)
                {
                    return "raw_record_segment_bytes must be greater than 0";
                }
                if (config.raw_record_max_total_bytes == 0U)
                {
                    return "raw_record_max_total_bytes must be greater than 0";
                }
                if (config.raw_record_segment_bytes > config.raw_record_max_total_bytes)
                {
                    return "raw_record_segment_bytes must be less than or equal to raw_record_max_total_bytes";
                }
            }
            if (config.log_output == "file" && config.log_file_path.empty())
            {
                return "log_file_path must not be empty when log_output=file";
            }
            if (config.protocol_udp_packet_size == 0U)
            {
                return "protocol_udp_packet_size must be greater than 0";
            }
            if (config.protocol_channels_per_prt == 0U)
            {
                return "protocol_channels_per_prt must be greater than 0";
            }
            if (config.protocol_packets_per_channel == 0U)
            {
                return "protocol_packets_per_channel must be greater than 0";
            }
            return {};
        }

        void print_dry_run(const RxConfig &config)
        {
            std::cout << "[dry-run]" << std::endl;
            std::cout << "backend=" << config.backend_name << std::endl;
            std::cout << "config_path=" << config.config_path << std::endl;
            std::cout << "capture_enabled=" << (config.capture_enabled ? "true" : "false") << std::endl;
            std::cout << "capture_output_dir=" << effective_capture_output_dir(config) << std::endl;
            std::cout << "capture_index_filename=" << config.capture_index_filename << std::endl;
            std::cout << "capture_data_filename=" << config.capture_data_filename << std::endl;
            std::cout << "raw_record_enabled=" << (config.raw_record_enabled ? "true" : "false") << std::endl;
            std::cout << "raw_record_output_dir=" << config.raw_record_output_dir << std::endl;
            std::cout << "raw_record_file_prefix=" << config.raw_record_file_prefix << std::endl;
            std::cout << "raw_record_ring_slots=" << config.raw_record_ring_slots << std::endl;
            std::cout << "raw_record_writer_batch_size=" << config.raw_record_writer_batch_size << std::endl;
            std::cout << "raw_record_max_frame_bytes=" << config.raw_record_max_frame_bytes << std::endl;
            std::cout << "raw_record_segment_bytes=" << config.raw_record_segment_bytes << std::endl;
            std::cout << "raw_record_max_total_bytes=" << config.raw_record_max_total_bytes << std::endl;
            std::cout << "interface=" << config.interface_name << std::endl;
            std::cout << "receiver_ipv4=" << config.receiver_ipv4 << std::endl;
            std::cout << "allowed_source_ipv4=" << config.allowed_source_ipv4 << std::endl;
            std::cout << "allowed_dest_port=" << config.allowed_dest_port << std::endl;
            std::cout << "queue_id=" << config.queue_id << std::endl;
            std::cout << "duration_seconds=" << config.duration_seconds << std::endl;
            std::cout << "max_burst=" << config.max_burst << std::endl;
            std::cout << "packet_size_bytes=" << config.packet_size_bytes << std::endl;
            std::cout << "protocol_udp_packet_size=" << config.protocol_udp_packet_size << std::endl;
            std::cout << "protocol_channels_per_prt=" << config.protocol_channels_per_prt << std::endl;
            std::cout << "protocol_packets_per_channel=" << config.protocol_packets_per_channel << std::endl;
            std::cout << "xdp_bind_mode=" << config.xdp_bind_mode << std::endl;
            std::cout << "run_until_stopped=" << (config.run_until_stopped ? "true" : "false") << std::endl;
            std::cout << "status_interval_seconds=" << config.status_interval_seconds << std::endl;
            std::cout << "log_level=" << config.log_level << std::endl;
            std::cout << "log_output=" << config.log_output << std::endl;
            std::cout << "log_file_path=" << config.log_file_path << std::endl;
            std::cout << "feedback_enabled=" << (config.feedback_enabled ? "true" : "false") << std::endl;
            std::cout << "feedback_host=" << config.feedback_host << std::endl;
            std::cout << "feedback_bind_host=" << config.feedback_bind_host << std::endl;
            std::cout << "feedback_port=" << config.feedback_port << std::endl;
        }

    } // namespace

    int run_app(const std::string &backend_name, int argc, char **argv)
    {
        try
        {
            const CliArgs args = parse_cli_args(argc, argv);
            const char *program_name = argc > 0 ? argv[0] : "rx_receiver_dpdk";

            if (!args.valid)
            {
                if (!args.error_message.empty())
                {
                    std::cerr << args.error_message << std::endl;
                }
                print_usage(std::cerr, program_name);
                return 1;
            }

            if (args.help)
            {
                print_usage(std::cout, program_name);
                return 0;
            }

            if (args.config_path.empty() && !args.dry_run)
            {
                std::cerr << "missing required argument: --config" << std::endl;
                print_usage(std::cerr, program_name);
                return 1;
            }

            ReceiveContext context;
            context.config = build_effective_config(backend_name, args);

            const std::string validation_error = validate_config(context.config);
            if (!validation_error.empty())
            {
                std::cerr << "invalid config: " << validation_error << std::endl;
                return 1;
            }

            if (args.dry_run)
            {
                print_dry_run(context.config);
                return 0;
            }

            context.backend = make_backend(backend_name);
            context.metrics = std::make_unique<MetricsCollector>();

            ReceiveRunner runner;
            if (context.config.run_until_stopped)
            {
                runner.set_status_output(&std::cout);
            }
            const RunSummary summary = runner.run(context);
            if (!summary.human_summary.empty())
            {
                std::cout << summary.human_summary;
            }
            else
            {
                std::cout << "运行结果： " << (summary.run_status == "success" ? "成功" : "失败") << std::endl;
            }
            if (summary.run_status != "success")
            {
                std::cerr << "run failed: " << summary.error_message << std::endl;
                return summary.run_status == "unavailable" ? 2 : 1;
            }
        }
        catch (const std::exception &ex)
        {
            std::cerr << "run failed: " << ex.what() << std::endl;
            return 1;
        }

        return 0;
    }

} // namespace rxtech
