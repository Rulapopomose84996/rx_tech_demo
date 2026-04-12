#pragma once

#include <memory>

#include "rxtech/metrics.h"
#include "rxtech/rx_backend.h"
#include "rxtech/rx_config.h"

namespace rxtech
{

    struct ReceiveContext
    {
        RxConfig config;
        BackendPtr backend;
        std::unique_ptr<MetricsCollector> metrics;
    };

} // namespace rxtech
