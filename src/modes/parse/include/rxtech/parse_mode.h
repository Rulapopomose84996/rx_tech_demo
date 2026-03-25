#pragma once

#include "rxtech/mode_processor.h"
#include "rxtech/reassembly.h"

namespace rxtech {

class ParseMode final : public IModeProcessor {
public:
    explicit ParseMode(std::uint32_t reassembly_timeout_ms = 1000U);

    std::string name() const override;
    void process(RxBurst& burst, IMetricsCollector& metrics) override;

private:
    BlockReassembler reassembler_;
};

}  // namespace rxtech
