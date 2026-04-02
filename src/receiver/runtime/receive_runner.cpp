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

namespace rxtech
{

    namespace
    {

        std::atomic<bool> g_stop_requested{false};

        class SignalHandlerGuard
        {
        public:
            SignalHandlerGuard()
            {
                previous_sigint_ = std::signal(SIGINT, signal_handler);
                previous_sigterm_ = std::signal(SIGTERM, signal_handler);
            }

            ~SignalHandlerGuard()
            {
                std::signal(SIGINT, previous_sigint_);
                std::signal(SIGTERM, previous_sigterm_);
            }

        private:
            using SignalHandler = void (*)(int);

            static void signal_handler(int)
            {
                g_stop_requested.store(true);
            }

            SignalHandler previous_sigint_ = SIG_DFL;
            SignalHandler previous_sigterm_ = SIG_DFL;
        };

        bool is_path_separator(char ch)
        {
            return ch == '/' || ch == '\\';
        }

        std::string path_filename(const std::string &path)
        {
            std::string normalized = path;
            while (!normalized.empty() && is_path_separator(normalized.back()))
            {
                normalized.pop_back();
            }
            const std::size_t pos = normalized.find_last_of("/\\");
            return pos == std::string::npos ? normalized : normalized.substr(pos + 1U);
        }

        std::string path_parent(const std::string &path)
        {
            std::string normalized = path;
            while (!normalized.empty() && is_path_separator(normalized.back()))
            {
                normalized.pop_back();
            }
            const std::size_t pos = normalized.find_last_of("/\\");
            return pos == std::string::npos ? std::string{} : normalized.substr(0U, pos);
        }

        std::string join_path(const std::string &base, const std::string &name)
        {
            if (base.empty())
            {
                return name;
            }
            if (is_path_separator(base.back()))
            {
                return base + name;
            }
            return base + "/" + name;
        }

        std::string sanitize_run_label(std::string label)
        {
            for (char &ch : label)
            {
                const bool keep =
                    (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
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
                throw std::runtime_error("failed to format run timestamp");
            }
            return buffer;
        }

        std::string choose_run_suffix(const RxConfig &config)
        {
            const std::string capture_dir =
                config.capture_output_dir.empty() ? config.output_dir : config.capture_output_dir;
            std::string suffix = sanitize_run_label(path_filename(capture_dir));
            if (suffix == "run" && !config.backend_name.empty())
            {
                suffix = sanitize_run_label(config.backend_name);
            }
            return suffix;
        }

        std::string make_capture_run_dir(const std::string &capture_dir, const std::string &run_label)
        {
            const std::string parent = path_parent(capture_dir);
            return join_path(parent, run_label);
        }

        std::string make_raw_record_run_dir(const std::string &raw_record_dir, const std::string &run_label)
        {
            return join_path(raw_record_dir, run_label);
        }

        std::string make_log_run_path(const std::string &log_file_path, const std::string &run_label)
        {
            const std::string parent = path_parent(log_file_path);
            const std::string filename = path_filename(log_file_path);
            return join_path(join_path(parent, run_label), filename);
        }

        void create_directory_if_needed(const std::string &path)
        {
            if (path.empty())
            {
                return;
            }

            const int result = mkdir(path.c_str(), 0755);
            if (result != 0 && errno != EEXIST)
            {
                throw std::runtime_error("failed to create directory: " + path);
            }
        }

        void ensure_parent_directory(const std::string &file_path)
        {
            std::string normalized = file_path;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            const std::size_t separator_pos = normalized.find_last_of('/');
            if (separator_pos == std::string::npos)
            {
                return;
            }

            const std::string parent = normalized.substr(0U, separator_pos);
            if (parent.empty())
            {
                return;
            }

            std::size_t start = 0;
            if (parent.size() >= 2U && parent[1] == ':')
            {
                start = 2U;
            }
            else if (is_path_separator(parent[0]))
            {
                start = 1U;
            }

            std::string current = parent.substr(0U, start);
            while (start < parent.size())
            {
                while (start < parent.size() && is_path_separator(parent[start]))
                {
                    if (current.empty())
                    {
                        current.push_back('/');
                    }
                    ++start;
                }
                const std::size_t next = parent.find('/', start);
                const std::string part = parent.substr(start, next == std::string::npos ? std::string::npos : next - start);
                if (!part.empty())
                {
                    if (!current.empty() && !is_path_separator(current.back()) && current.back() != ':')
                    {
                        current.push_back('/');
                    }
                    current += part;
                    create_directory_if_needed(current);
                }
                if (next == std::string::npos)
                {
                    break;
                }
                start = next + 1U;
            }
        }

        RunSummary make_unavailable_summary(const ReceiveContext &context,
                                            const BackendInitResult &init_result)
        {
            RunSummary summary;
            summary.backend = context.backend != nullptr ? context.backend->name() : context.config.backend_name;
            summary.run_status = init_result.available ? "error" : "unavailable";
            summary.error_message = init_result.reason;
            summary.backend_available = init_result.available;
            summary.backend_status = init_result.available ? "available" : "unavailable";
            summary.backend_reason = init_result.reason;
            return summary;
        }

    } // namespace

    void request_receive_stop()
    {
        g_stop_requested.store(true);
    }

    void reset_receive_stop()
    {
        g_stop_requested.store(false);
    }

    void prepare_run_artifact_paths(RxConfig &config)
    {
        if (config.run_artifacts_prepared)
        {
            return;
        }

        const std::string run_suffix = choose_run_suffix(config);
        config.run_label = make_run_timestamp() + "_" + run_suffix;

        const std::string capture_dir =
            config.capture_output_dir.empty() ? config.output_dir : config.capture_output_dir;
        if (!capture_dir.empty())
        {
            const std::string run_capture_dir = make_capture_run_dir(capture_dir, config.run_label);
            config.output_dir = run_capture_dir;
            config.capture_output_dir = run_capture_dir;
        }

        if (config.raw_record_enabled && !config.raw_record_output_dir.empty())
        {
            config.raw_record_output_dir = make_raw_record_run_dir(config.raw_record_output_dir, config.run_label);
        }

        if (config.log_output == "file" && !config.log_file_path.empty())
        {
            config.log_file_path = make_log_run_path(config.log_file_path, config.run_label);
        }

        config.run_artifacts_prepared = true;
    }

    void ReceiveRunner::set_status_output(std::ostream *output)
    {
        status_output_ = output;
    }

    RunSummary ReceiveRunner::run(ReceiveContext &context)
    {
        if (!context.backend || !context.metrics)
        {
            throw std::runtime_error("receive context is incomplete");
        }

        reset_receive_stop();
        SignalHandlerGuard signal_guard;
        prepare_run_artifact_paths(context.config);

        const BackendInitResult init_result = context.backend->init(context.config);
        if (!init_result.ok)
        {
            RunSummary summary = make_unavailable_summary(context, init_result);
            context.backend->shutdown();
            return summary;
        }

        try
        {
            const std::string output_dir =
                context.config.capture_output_dir.empty() ? context.config.output_dir : context.config.capture_output_dir;
            const bool capture_enabled = context.config.capture_enabled;
            const std::string capture_packets_path =
                capture_enabled ? (output_dir + "/" + context.config.capture_data_filename) : std::string{};
            const std::string capture_index_path =
                capture_enabled ? (output_dir + "/" + context.config.capture_index_filename) : std::string{};
            std::ofstream capture_packets_stream;
            std::ofstream capture_index_stream;
            std::ostringstream capture_packets_sink;
            std::ostringstream capture_index_sink;
            if (capture_enabled)
            {
                ensure_parent_directory(capture_packets_path);
                ensure_parent_directory(capture_index_path);

                capture_packets_stream.open(capture_packets_path, std::ios::binary | std::ios::trunc);
                capture_index_stream.open(capture_index_path, std::ios::trunc);
                if (!capture_packets_stream.is_open())
                {
                    throw std::runtime_error("failed to open capture file: " + capture_packets_path);
                }
                if (!capture_index_stream.is_open())
                {
                    throw std::runtime_error("failed to open capture index file: " + capture_index_path);
                }
            }

            RawFrameRecorder raw_frame_recorder(context.config);
            if (raw_frame_recorder.enabled())
            {
                raw_frame_recorder.start();
            }

            OwnerLoop owner_loop;
            owner_loop.set_status_output(status_output_);
            CaptureArtifacts artifacts;
            artifacts.packet_stream = capture_enabled ? static_cast<std::ostream *>(&capture_packets_stream)
                                                      : static_cast<std::ostream *>(&capture_packets_sink);
            artifacts.index_stream = capture_enabled ? static_cast<std::ostream *>(&capture_index_stream)
                                                     : static_cast<std::ostream *>(&capture_index_sink);
            artifacts.raw_frame_recorder = raw_frame_recorder.enabled() ? &raw_frame_recorder : nullptr;

            const auto start_time = std::chrono::steady_clock::now();
            const auto deadline = start_time + std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.duration_seconds));
            RunSummary summary = owner_loop.run(
                context,
                artifacts,
                [&]()
                {
                    return context.config.run_until_stopped ? g_stop_requested.load() : (std::chrono::steady_clock::now() >= deadline);
                });

            raw_frame_recorder.stop();
            const std::string raw_record_error = raw_frame_recorder.error_message();
            if (!raw_record_error.empty())
            {
                throw std::runtime_error("raw frame recorder failed: " + raw_record_error);
            }

            const RawFrameRecorderStats raw_record_stats = raw_frame_recorder.snapshot();
            summary.backend_available = true;
            summary.backend_status = "available";
            summary.capture_packets_path = capture_packets_path;
            summary.capture_index_path = capture_index_path;
            summary.captured_packets = artifacts.captured_packets;
            summary.captured_bytes = artifacts.captured_bytes;
            summary.recorded_packets = artifacts.recorded_packets;
            summary.recorded_bytes = artifacts.recorded_bytes;
            summary.run_artifact_dir = output_dir;
            summary.raw_record_output_dir = raw_frame_recorder.enabled() ? raw_frame_recorder.output_dir() : std::string{};
            summary.raw_record_latest_file_path = raw_record_stats.latest_file_path;
            summary.raw_record_written_frames = raw_record_stats.written_frames;
            summary.raw_record_written_bytes = raw_record_stats.written_bytes;
            summary.raw_record_dropped_frames = raw_record_stats.dropped_frames;
            summary.raw_record_dropped_bytes = raw_record_stats.dropped_bytes;
            summary.raw_record_retained_bytes = raw_record_stats.retained_bytes;
            summary.raw_record_queue_high_watermark = raw_record_stats.queue_high_watermark;
            summary.human_summary = build_run_human_summary(summary);

            context.backend->shutdown();
            return summary;
        }
        catch (...)
        {
            context.backend->shutdown();
            throw;
        }
    }

} // namespace rxtech
