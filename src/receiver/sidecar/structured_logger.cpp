#include "internal/structured_logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

#if defined(RXTECH_HAS_SPDLOG) && RXTECH_HAS_SPDLOG
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace rxtech
{

    namespace
    {

        std::mutex g_logger_mutex;
        bool g_logger_enabled = false;
        StructuredLogLevel g_min_level = StructuredLogLevel::info;
        std::string g_log_format = "json";
        std::ofstream g_log_file_stream;
        std::ostream *g_fallback_stream = nullptr;
        std::string g_backend_name = "disabled";

#if defined(RXTECH_HAS_SPDLOG) && RXTECH_HAS_SPDLOG
        std::shared_ptr<spdlog::logger> g_spdlog_logger;
#endif

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

        bool should_emit(StructuredLogLevel level)
        {
            return g_logger_enabled && structured_log_level_rank(level) >= structured_log_level_rank(g_min_level);
        }

        const char *structured_log_level_name(StructuredLogLevel level) noexcept
        {
            switch (level)
            {
            case StructuredLogLevel::debug:
                return "debug";
            case StructuredLogLevel::info:
                return "info";
            case StructuredLogLevel::warn:
                return "warn";
            case StructuredLogLevel::error:
                return "error";
            }
            return "info";
        }

        std::string format_timestamp()
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

        nlohmann::json make_record(StructuredLogLevel level, const std::string &event, const nlohmann::json &fields)
        {
            nlohmann::json record = fields.is_object() ? fields : nlohmann::json::object();
            record["timestamp"] = format_timestamp();
            record["level"] = structured_log_level_name(level);
            record["event"] = event;
            return record;
        }

        std::string render_text_record(const nlohmann::json &record)
        {
            std::ostringstream out;
            out << '[' << record.value("timestamp", std::string{}) << "] " << record.value("level", std::string{"info"})
                << ' ' << record.value("event", std::string{"log"});
            for (auto it = record.begin(); it != record.end(); ++it)
            {
                if (it.key() == "timestamp" || it.key() == "level" || it.key() == "event")
                {
                    continue;
                }
                out << ' ' << it.key() << '=' << it.value().dump();
            }
            return out.str();
        }

#if defined(RXTECH_HAS_SPDLOG) && RXTECH_HAS_SPDLOG
        spdlog::level::level_enum to_spdlog_level(StructuredLogLevel level)
        {
            switch (level)
            {
            case StructuredLogLevel::debug:
                return spdlog::level::debug;
            case StructuredLogLevel::warn:
                return spdlog::level::warn;
            case StructuredLogLevel::error:
                return spdlog::level::err;
            case StructuredLogLevel::info:
            default:
                return spdlog::level::info;
            }
        }
#endif

    } // namespace

    void configure_structured_logger(const RxConfig &config)
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);

#if defined(RXTECH_HAS_SPDLOG) && RXTECH_HAS_SPDLOG
        g_spdlog_logger.reset();
#endif
        g_log_file_stream.close();
        g_fallback_stream = nullptr;
        g_logger_enabled = false;
        g_backend_name = "disabled";

        const std::string output = config.operations.structured_log_output;
        if (output == "disabled")
        {
            return;
        }

        g_min_level = parse_structured_log_level(config.operations.log_level);
        g_log_format = config.operations.structured_log_format;

#if defined(RXTECH_HAS_SPDLOG) && RXTECH_HAS_SPDLOG
        try
        {
            if (output == "stdout")
            {
                g_spdlog_logger = std::make_shared<spdlog::logger>(
                    "rxtech_structured_logger", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
            }
            else if (output == "stderr")
            {
                g_spdlog_logger = std::make_shared<spdlog::logger>(
                    "rxtech_structured_logger", std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
            }
            else if (output == "file" && !config.operations.structured_log_file_path.empty())
            {
                g_spdlog_logger = std::make_shared<spdlog::logger>(
                    "rxtech_structured_logger", std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                                                    config.operations.structured_log_file_path, true));
            }

            if (g_spdlog_logger != nullptr)
            {
                g_spdlog_logger->set_level(to_spdlog_level(g_min_level));
                g_spdlog_logger->set_pattern("%v");
                g_logger_enabled = true;
                g_backend_name = "spdlog";
                return;
            }
        }
        catch (...)
        {
            g_spdlog_logger.reset();
        }
#endif

        if (output == "stdout")
        {
            g_fallback_stream = &std::cout;
        }
        else if (output == "stderr")
        {
            g_fallback_stream = &std::cerr;
        }
        else if (output == "file" && !config.operations.structured_log_file_path.empty())
        {
            g_log_file_stream.open(config.operations.structured_log_file_path, std::ios::out | std::ios::app);
            if (g_log_file_stream.is_open())
            {
                g_fallback_stream = &g_log_file_stream;
            }
        }

        if (g_fallback_stream != nullptr)
        {
            g_logger_enabled = true;
            g_backend_name = "builtin";
        }
    }

    void shutdown_structured_logger()
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
#if defined(RXTECH_HAS_SPDLOG) && RXTECH_HAS_SPDLOG
        g_spdlog_logger.reset();
#endif
        if (g_log_file_stream.is_open())
        {
            g_log_file_stream.flush();
            g_log_file_stream.close();
        }
        g_fallback_stream = nullptr;
        g_logger_enabled = false;
        g_backend_name = "disabled";
    }

    const char *structured_logger_backend_name() noexcept
    {
        return g_backend_name.c_str();
    }

    void structured_log(StructuredLogLevel level, const std::string &event, const nlohmann::json &fields)
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        if (!should_emit(level))
        {
            return;
        }

        const nlohmann::json record = make_record(level, event, fields);
        const std::string payload = g_log_format == "text" ? render_text_record(record) : record.dump();

#if defined(RXTECH_HAS_SPDLOG) && RXTECH_HAS_SPDLOG
        if (g_spdlog_logger != nullptr)
        {
            g_spdlog_logger->log(to_spdlog_level(level), payload);
            g_spdlog_logger->flush();
            return;
        }
#endif

        if (g_fallback_stream != nullptr)
        {
            (*g_fallback_stream) << payload << '\n';
            g_fallback_stream->flush();
        }
    }

} // namespace rxtech
