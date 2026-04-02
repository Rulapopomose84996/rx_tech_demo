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

        const BackendInitResult init_result = context.backend->init(context.config);
        if (!init_result.ok)
        {
            RunSummary summary = make_unavailable_summary(context, init_result);
            context.backend->shutdown();
            return summary;
        }

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

        OwnerLoop owner_loop;
        owner_loop.set_status_output(status_output_);
        CaptureArtifacts artifacts;
        artifacts.packet_stream = capture_enabled ? static_cast<std::ostream *>(&capture_packets_stream)
                                                  : static_cast<std::ostream *>(&capture_packets_sink);
        artifacts.index_stream = capture_enabled ? static_cast<std::ostream *>(&capture_index_stream)
                                                 : static_cast<std::ostream *>(&capture_index_sink);

        const auto start_time = std::chrono::steady_clock::now();
        const auto deadline = start_time + std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.duration_seconds));
        RunSummary summary = owner_loop.run(
            context,
            artifacts,
            [&]()
            {
                return context.config.run_until_stopped ? g_stop_requested.load() : (std::chrono::steady_clock::now() >= deadline);
            });

        summary.backend_available = true;
        summary.backend_status = "available";
        summary.capture_packets_path = capture_packets_path;
        summary.capture_index_path = capture_index_path;
        summary.captured_packets = artifacts.captured_packets;
        summary.captured_bytes = artifacts.captured_bytes;
        summary.recorded_packets = artifacts.recorded_packets;
        summary.recorded_bytes = artifacts.recorded_bytes;

        context.backend->shutdown();
        return summary;
    }

} // namespace rxtech
