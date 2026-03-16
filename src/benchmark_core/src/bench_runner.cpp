#include "rxtech/bench_runner.h"

#include <stdexcept>

#include "rxtech/report_writer.h"

namespace rxtech {

RunSummary BenchRunner::run(BenchContext& context) {
    if (!context.backend || !context.mode || !context.metrics) {
        throw std::runtime_error("bench context is incomplete");
    }

    if (!context.backend->init(context.config)) {
        throw std::runtime_error("backend init failed");
    }

    RxBurst burst;
    context.backend->recv_burst(burst, context.config.max_burst);
    context.mode->process(burst, *context.metrics);
    context.backend->release_burst(burst);

    RunSummary summary = context.metrics->finalize(
        context.backend->name(),
        context.mode->name(),
        context.scenario.scenario_name,
        context.config.duration_seconds);

    write_summary_json(summary, context.config.output_dir);
    write_summary_csv(summary, context.config.output_dir);

    context.backend->shutdown();
    return summary;
}

}  // namespace rxtech
