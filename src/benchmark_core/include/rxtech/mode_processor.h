#pragma once

#include <memory>
#include <string>

#include "rxtech/packet_desc.h"

namespace rxtech {

class IMetricsCollector;

class IModeProcessor {
public:
    virtual ~IModeProcessor() = default;

    virtual std::string name() const = 0;
    virtual void process(RxBurst& burst, IMetricsCollector& metrics) = 0;
};

using ModeProcessorPtr = std::unique_ptr<IModeProcessor>;

}  // namespace rxtech
