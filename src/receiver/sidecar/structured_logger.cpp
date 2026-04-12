#include "internal/structured_logger.h"

#include <chrono>
#include <cerrno>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "internal/event_logger.h"

namespace rxtech
{

    namespace
    {

        std::mutex g_logger_mutex;
        StructuredLogLevel g_min_level = StructuredLogLevel::info;
        std::unique_ptr<EventLogger> g_event_logger;
        std::vector<std::unique_ptr<IEventSink>> g_owned_sinks;
        std::string g_backend_name = "disabled";
        std::string g_events_path;

        bool is_directory_separator(const char ch) noexcept
        {
            return ch == '/' || ch == '\\';
        }

        std::string parent_directory(const std::string &path)
        {
            const std::string::size_type pos = path.find_last_of("/\\");
            if (pos == std::string::npos)
            {
                return std::string();
            }
            if (pos == 0)
            {
                return path.substr(0, 1);
            }
            return path.substr(0, pos);
        }

        bool directory_exists(const std::string &path)
        {
            if (path.empty())
            {
                return false;
            }
#ifdef _WIN32
            struct _stat info
            {
            };
            return _stat(path.c_str(), &info) == 0 && (info.st_mode & _S_IFDIR) != 0;
#else
            struct stat info
            {
            };
            return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
        }

        bool create_single_directory(const std::string &path)
        {
            if (path.empty() || directory_exists(path))
            {
                return true;
            }
#ifdef _WIN32
            return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
            return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
        }

        bool ensure_directory_tree(const std::string &path)
        {
            if (path.empty() || directory_exists(path))
            {
                return true;
            }

            std::string current;
            std::size_t index = 0;

            if (path.size() >= 2 && path[1] == ':')
            {
                current = path.substr(0, 2);
                index = 2;
                while (index < path.size() && is_directory_separator(path[index]))
                {
                    current.push_back(path[index]);
                    ++index;
                }
            }
            else if (is_directory_separator(path[0]))
            {
                current.push_back(path[0]);
                index = 1;
                while (index < path.size() && is_directory_separator(path[index]))
                {
                    ++index;
                }
            }

            while (index < path.size())
            {
                while (index < path.size() && is_directory_separator(path[index]))
                {
                    ++index;
                }
                if (index >= path.size())
                {
                    break;
                }

                const std::size_t next = path.find_first_of("/\\", index);
                const std::string segment = path.substr(index, next == std::string::npos ? std::string::npos : next - index);
                if (!segment.empty())
                {
                    if (!current.empty() && !is_directory_separator(current[current.size() - 1]))
                    {
                        current.push_back('/');
                    }
                    current += segment;
                    if (!create_single_directory(current))
                    {
                        return false;
                    }
                }

                if (next == std::string::npos)
                {
                    break;
                }
                index = next + 1;
            }

            return true;
        }

        void ensure_parent_directory(const std::string &file_path)
        {
            const std::string parent = parent_directory(file_path);
            if (!parent.empty())
            {
                ensure_directory_tree(parent);
            }
        }

        StructuredLogLevel parse_structured_log_level(const std::string &level)
        {
            if (level == "debug")
            {
                return StructuredLogLevel::debug;
            }
            if (level == "warn" || level == "warning")
            {
                return StructuredLogLevel::warn;
            }
            if (level == "error")
            {
                return StructuredLogLevel::error;
            }
            return StructuredLogLevel::info;
        }

        std::string format_wall_clock_now()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
            std::tm local_time{};
#ifdef _WIN32
            localtime_s(&local_time, &now_time);
#else
            localtime_r(&now_time, &local_time);
#endif
            std::ostringstream out;
            out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
            return out.str();
        }

        std::uint64_t monotonic_now_ns()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }

        const char *build_mode_name() noexcept
        {
#ifdef NDEBUG
            return "release";
#else
            return "debug";
#endif
        }

        int structured_log_level_rank(StructuredLogLevel level)
        {
            switch (level)
            {
            case StructuredLogLevel::debug:
                return 0;
            case StructuredLogLevel::info:
                return 1;
            case StructuredLogLevel::warn:
                return 2;
            case StructuredLogLevel::error:
                return 3;
            }
            return 1;
        }

        class OstreamEventSink final : public IEventSink
        {
          public:
            explicit OstreamEventSink(std::ostream *stream) : stream_(stream) {}

            void write_line(const std::string &line) override
            {
                if (stream_ != nullptr)
                {
                    (*stream_) << line << '\n';
                }
            }

            void flush() override
            {
                if (stream_ != nullptr)
                {
                    stream_->flush();
                }
            }

          private:
            std::ostream *stream_ = nullptr;
        };

        class FileEventSink final : public IEventSink
        {
          public:
            explicit FileEventSink(std::string path) : path_(std::move(path)), stream_(path_, std::ios::out | std::ios::app) {}

            bool is_open() const noexcept
            {
                return stream_.is_open();
            }

            void write_line(const std::string &line) override
            {
                if (stream_.is_open())
                {
                    stream_ << line << '\n';
                }
            }

            void flush() override
            {
                if (stream_.is_open())
                {
                    stream_.flush();
                }
            }

          private:
            std::string path_;
            std::ofstream stream_;
        };

    } // namespace

    std::string default_events_log_path(const RxConfig &config)
    {
        const std::string output_dir = config.operations.output_dir.empty() ? "results" : config.operations.output_dir;
        return output_dir + "/events.jsonl";
    }

    void configure_structured_logger(const RxConfig &config)
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        g_event_logger.reset();
        g_owned_sinks.clear();
        g_backend_name = "disabled";
        g_events_path.clear();

        const std::string output = config.operations.structured_log_output;
        if (output == "disabled")
        {
            return;
        }

        g_min_level = parse_structured_log_level(config.operations.log_level);

        g_events_path = default_events_log_path(config);
        ensure_parent_directory(g_events_path);

        auto primary_sink = std::make_unique<FileEventSink>(g_events_path);
        if (primary_sink->is_open())
        {
            g_owned_sinks.push_back(std::move(primary_sink));
        }

        if (output == "stdout")
        {
            g_owned_sinks.push_back(std::make_unique<OstreamEventSink>(&std::cout));
        }
        else if (output == "stderr")
        {
            g_owned_sinks.push_back(std::make_unique<OstreamEventSink>(&std::cerr));
        }
        else if (output == "file" && !config.operations.structured_log_file_path.empty())
        {
            ensure_parent_directory(config.operations.structured_log_file_path);
            auto sink = std::make_unique<FileEventSink>(config.operations.structured_log_file_path);
            if (sink->is_open())
            {
                g_owned_sinks.push_back(std::move(sink));
            }
        }

        if (!g_owned_sinks.empty())
        {
            EventLoggerConfig event_config;
            event_config.min_level = g_min_level;
            for (const auto &sink : g_owned_sinks)
            {
                event_config.sinks.push_back(sink.get());
            }
            g_event_logger = std::make_unique<EventLogger>(std::move(event_config));
            g_backend_name = "event_logger";
        }
    }

    void shutdown_structured_logger()
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        if (g_event_logger != nullptr)
        {
            g_event_logger->flush();
        }
        g_event_logger.reset();
        g_owned_sinks.clear();
        g_backend_name = "disabled";
        g_events_path.clear();
    }

    const char *structured_logger_backend_name() noexcept
    {
        return g_backend_name.c_str();
    }

    std::string structured_logger_events_path()
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        return g_events_path;
    }

    void structured_log(StructuredLogLevel level, const std::string &event, const nlohmann::json &fields)
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        if (g_event_logger == nullptr || structured_log_level_rank(level) < structured_log_level_rank(g_min_level))
        {
            return;
        }

        EventEnvelope envelope;
        envelope.event = event;
        envelope.level = level;
        envelope.ts_monotonic_ns = monotonic_now_ns();
        envelope.ts_wall = format_wall_clock_now();
        envelope.build_mode = build_mode_name();
        envelope.payload = fields.is_object() ? fields : nlohmann::json::object();
        if (envelope.payload.contains("backend") && envelope.payload["backend"].is_string())
        {
            envelope.backend = envelope.payload["backend"].get<std::string>();
        }
        if (envelope.payload.contains("run_id") && envelope.payload["run_id"].is_string())
        {
            envelope.run_id = envelope.payload["run_id"].get<std::string>();
        }
        g_event_logger->emit(envelope);
        g_event_logger->flush();
    }

} // namespace rxtech
