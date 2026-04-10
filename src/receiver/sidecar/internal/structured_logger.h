#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "rxtech/rx_config.h"

namespace rxtech
{

    enum class StructuredLogLevel
    {
        debug,
        info,
        warn,
        error,
    };

    void configure_structured_logger(const RxConfig &config);
    void shutdown_structured_logger();
    const char *structured_logger_backend_name() noexcept;
    void structured_log(StructuredLogLevel level, const std::string &event,
                        const nlohmann::json &fields = nlohmann::json::object());

} // namespace rxtech
