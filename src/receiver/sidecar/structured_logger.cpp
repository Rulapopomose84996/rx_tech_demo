#include "internal/structured_logger.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>

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
            auto sink = std::make_unique<FileEventSink>(config.operations.structured_log_file_path);
            if (sink->is_open())
            {
                g_events_path = config.operations.structured_log_file_path;
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
        envelope.payload = fields.is_object() ? fields : nlohmann::json::object();
        g_event_logger->emit(envelope);
        g_event_logger->flush();
    }

} // namespace rxtech
