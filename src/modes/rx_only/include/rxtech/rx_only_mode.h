#pragma once

#include "rxtech/mode_processor.h"

namespace rxtech {

class RxOnlyMode final : public IModeProcessor {
public:
    std::string name() const override;
    void process(RxBurst& burst, IMetricsCollector& metrics) override;
};

}  // namespace rxtech
