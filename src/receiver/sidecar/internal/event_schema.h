#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "structured_logger.h"

namespace rxtech
{

    inline constexpr std::uint32_t kEventSchemaVersion = 1U;

    struct EventEnvelope
    {
        std::string event;
        StructuredLogLevel level = StructuredLogLevel::info;
        std::uint64_t ts_monotonic_ns = 0U;
        std::string ts_wall;
        std::string run_id;
        std::string backend;
        std::string build_mode;
        nlohmann::json payload = nlohmann::json::object();
    };

    const char *structured_log_level_name(StructuredLogLevel level) noexcept;
    std::string render_event_jsonl(const EventEnvelope &event, std::uint32_t schema_version);

} // namespace rxtech
