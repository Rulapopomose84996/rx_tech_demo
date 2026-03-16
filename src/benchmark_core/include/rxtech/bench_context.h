#pragma once

#include <memory>

#include "rxtech/metrics.h"
#include "rxtech/mode_processor.h"
#include "rxtech/rx_backend.h"
#include "rxtech/rx_config.h"
#include "rxtech/scenario.h"

namespace rxtech {

struct BenchContext {
    RxConfig config;
    Scenario scenario;
    BackendPtr backend;
    ModeProcessorPtr mode;
    std::unique_ptr<IMetricsCollector> metrics;
};

}  // namespace rxtech
