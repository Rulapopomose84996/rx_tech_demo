#pragma once

#include "rxtech/bench_context.h"

namespace rxtech {

class BenchRunner {
public:
    RunSummary run(BenchContext& context);
};

}  // namespace rxtech
