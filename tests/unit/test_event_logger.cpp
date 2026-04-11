#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "event_logger.h"
#include "event_schema.h"

namespace
{
    class VectorSink final : public rxtech::IEventSink
    {
      public:
        void write_line(const std::string &line) override
        {
            lines.push_back(line);
        }

        void flush() override {}

        std::vector<std::string> lines;
    };
} // namespace

int main()
{
    rxtech::EventEnvelope event;
    event.event = "run.started";
    event.level = rxtech::StructuredLogLevel::info;
    event.ts_monotonic_ns = 123456789ULL;
    event.ts_wall = "2026-04-11 21:00:00";
    event.run_id = "20260411_210000_socket_loopback";
    event.backend = "socket";
    event.build_mode = "debug";
    event.payload = {{"config_path", "configs/socket_loopback.conf"}};

    const std::string line = rxtech::render_event_jsonl(event, rxtech::kEventSchemaVersion);
    const nlohmann::json parsed = nlohmann::json::parse(line);
    assert(parsed.at("schema_version") == rxtech::kEventSchemaVersion);
    assert(parsed.at("event") == "run.started");
    assert(parsed.at("backend") == "socket");
    assert(parsed.at("payload").at("config_path") == "configs/socket_loopback.conf");

    VectorSink sink;
    rxtech::EventLoggerConfig config;
    config.min_level = rxtech::StructuredLogLevel::info;
    config.sinks.push_back(&sink);

    rxtech::EventLogger logger(config);
    logger.emit(event);
    assert(sink.lines.size() == 1U);

    event.level = rxtech::StructuredLogLevel::debug;
    logger.emit(event);
    assert(sink.lines.size() == 1U);
    return 0;
}
