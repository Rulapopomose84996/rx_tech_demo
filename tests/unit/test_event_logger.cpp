#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "event_logger.h"
#include "event_schema.h"
#include "rxtech/rx_config.h"
#include "structured_logger.h"

namespace
{
    void remove_directory_if_exists(const char *path)
    {
#ifdef _WIN32
        _rmdir(path);
#else
        rmdir(path);
#endif
    }

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

    const char *temp_path = "test_event_logger.jsonl";
    {
        std::remove(temp_path);
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.structured_log_output = "file";
        config.operations.structured_log_file_path = temp_path;
        config.operations.structured_log_format = "json";
        config.operations.log_level = "info";

        rxtech::configure_structured_logger(config);
        rxtech::structured_log(rxtech::StructuredLogLevel::info, "run.started",
                               {{"backend", "socket"}, {"config_path", "configs/socket_loopback.conf"}});
        rxtech::shutdown_structured_logger();

        std::ifstream input(temp_path);
        assert(input.is_open());
        std::string file_line;
        std::getline(input, file_line);
        const nlohmann::json parsed_file = nlohmann::json::parse(file_line);
        assert(parsed_file.at("event") == "run.started");
        assert(parsed_file.at("backend") == "socket");
        assert(parsed_file.at("payload").at("backend") == "socket");
    }
    std::remove(temp_path);

    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.output_dir = "results/stage1_case";
        config.operations.structured_log_output = "stderr";
        assert(rxtech::default_events_log_path(config) == "results/stage1_case/events.jsonl");
    }

    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.structured_log_output = "file";
        config.operations.structured_log_file_path = "test_event_logger_snapshot.jsonl";
        std::remove(config.operations.structured_log_file_path.c_str());

        rxtech::configure_structured_logger(config);
        rxtech::structured_log(rxtech::StructuredLogLevel::info, "status.snapshot",
                               {{"backend", "socket"},
                                {"traffic_state", "idle"},
                                {"window_rx_gbps", 0.0},
                                {"protocol_parsed_packets", 0U},
                                {"elapsed_seconds", 5U}});
        rxtech::shutdown_structured_logger();

        std::ifstream input(config.operations.structured_log_file_path);
        assert(input.is_open());
        std::string line;
        std::getline(input, line);
        const nlohmann::json parsed_snapshot = nlohmann::json::parse(line);
        assert(parsed_snapshot.at("event") == "status.snapshot");
        assert(parsed_snapshot.at("payload").at("traffic_state") == "idle");
        std::remove(config.operations.structured_log_file_path.c_str());
    }

    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.structured_log_output = "file";
        config.operations.structured_log_file_path = "test_traffic_transition.jsonl";
        std::remove(config.operations.structured_log_file_path.c_str());

        rxtech::configure_structured_logger(config);
        rxtech::structured_log(rxtech::StructuredLogLevel::warn, "traffic.interrupted",
                               {{"backend", "socket"},
                                {"last_valid_business_packet_wall", "2026-04-12 00:00:04"},
                                {"interrupt_timeout_ms", 3000},
                                {"current_state", "interrupted"}});
        rxtech::shutdown_structured_logger();

        std::ifstream input(config.operations.structured_log_file_path);
        assert(input.is_open());
        std::string line;
        std::getline(input, line);
        const nlohmann::json parsed_transition = nlohmann::json::parse(line);
        assert(parsed_transition.at("event") == "traffic.interrupted");
        assert(parsed_transition.at("payload").at("current_state") == "interrupted");
        std::remove(config.operations.structured_log_file_path.c_str());
    }

    {
        const std::string nested_dir = "tmp_test_event_logger";
        const std::string nested_subdir = nested_dir + "/nested";
        const std::string nested_path = nested_subdir + "/mirror.jsonl";
        std::remove(nested_path.c_str());
        remove_directory_if_exists(nested_subdir.c_str());
        remove_directory_if_exists(nested_dir.c_str());

        rxtech::RxConfig config = rxtech::load_default_config();
        config.operations.structured_log_output = "file";
        config.operations.structured_log_file_path = nested_path;

        rxtech::configure_structured_logger(config);
        rxtech::structured_log(rxtech::StructuredLogLevel::info, "run.started",
                               {{"backend", "socket"}, {"run_id", "nested-path-case"}});
        rxtech::shutdown_structured_logger();

        std::ifstream input(nested_path);
        assert(input.is_open());
        std::string line;
        std::getline(input, line);
        const nlohmann::json parsed_nested = nlohmann::json::parse(line);
        assert(parsed_nested.at("event") == "run.started");

        std::remove(nested_path.c_str());
        remove_directory_if_exists(nested_subdir.c_str());
        remove_directory_if_exists(nested_dir.c_str());
    }
    return 0;
}
