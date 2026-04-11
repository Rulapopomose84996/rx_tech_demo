#include "internal/event_logger.h"

#include <utility>

namespace rxtech
{

    namespace
    {

        int level_rank(StructuredLogLevel level)
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

    } // namespace

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

    std::string render_event_jsonl(const EventEnvelope &event, std::uint32_t schema_version)
    {
        nlohmann::json record;
        record["ts_wall"] = event.ts_wall;
        record["ts_monotonic_ns"] = event.ts_monotonic_ns;
        record["run_id"] = event.run_id;
        record["backend"] = event.backend;
        record["build_mode"] = event.build_mode;
        record["schema_version"] = schema_version;
        record["event"] = event.event;
        record["level"] = structured_log_level_name(event.level);
        record["payload"] = event.payload.is_object() ? event.payload : nlohmann::json::object();
        return record.dump();
    }

    EventLogger::EventLogger(EventLoggerConfig config) : config_(std::move(config)) {}

    void EventLogger::emit(const EventEnvelope &event)
    {
        if (level_rank(event.level) < level_rank(config_.min_level))
        {
            return;
        }

        const std::string line = render_event_jsonl(event, config_.schema_version);
        for (IEventSink *sink : config_.sinks)
        {
            if (sink != nullptr)
            {
                sink->write_line(line);
            }
        }
    }

    void EventLogger::flush()
    {
        for (IEventSink *sink : config_.sinks)
        {
            if (sink != nullptr)
            {
                sink->flush();
            }
        }
    }

} // namespace rxtech
