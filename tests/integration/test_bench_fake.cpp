#include <cassert>
#include <fstream>
#include <memory>

#include "rxtech/bench_runner.h"
#include "rxtech/metrics.h"
#include "rxtech/rx_config.h"
#include "rxtech/rx_only_mode.h"
#include "rxtech/scenario.h"
#include "rxtech/time_utils.h"

namespace {

class FakeBackend final : public rxtech::IRxBackend {
public:
    std::string name() const override {
        return "fake";
    }

    rxtech::BackendInitResult init(const rxtech::RxConfig&) override {
        rxtech::BackendInitResult result;
        result.ok = true;
        return result;
    }

    bool recv_burst(rxtech::RxBurst& burst, std::uint32_t) override {
        burst.packets.clear();
        ++calls_;
        rxtech::PacketDesc packet;
        packet.data = payload_.data();
        packet.len = static_cast<std::uint32_t>(payload_.size());
        packet.ts_ns = rxtech::steady_clock_now_ns();
        packet.queue_id = 0;
        burst.packets.push_back(packet);
        ++stats_.rx_packets;
        stats_.rx_bytes += packet.len;
        ++stats_.rx_polls;
        return true;
    }

    void release_burst(rxtech::RxBurst& burst) override {
        burst.packets.clear();
    }

    rxtech::BackendStats stats() const override {
        return stats_;
    }

    void shutdown() override {
    }

private:
    std::size_t calls_ = 0;
    rxtech::BackendStats stats_{};
    std::vector<std::uint8_t> payload_{128U, 0xABU};
};

class UnavailableBackend final : public rxtech::IRxBackend {
public:
    std::string name() const override {
        return "fake_unavailable";
    }

    rxtech::BackendInitResult init(const rxtech::RxConfig&) override {
        rxtech::BackendInitResult result;
        result.available = false;
        result.reason = "backend unavailable in test";
        return result;
    }

    bool recv_burst(rxtech::RxBurst&, std::uint32_t) override {
        return false;
    }

    void release_burst(rxtech::RxBurst& burst) override {
        burst.packets.clear();
    }

    rxtech::BackendStats stats() const override {
        return {};
    }

    void shutdown() override {
    }
};

}  // namespace

int main() {
    {
        rxtech::BenchContext context;
        context.config = rxtech::load_default_config();
        context.config.output_dir = "results/test_fake";
        context.scenario = rxtech::load_scenario("smoke");
        context.backend = std::make_unique<FakeBackend>();
        context.mode = std::make_unique<rxtech::RxOnlyMode>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::BenchRunner runner;
        const rxtech::RunSummary summary = runner.run(context);
        assert(summary.run_status == "success");
        assert(summary.total_step_count == 2U);
        assert(summary.measure_step_count == 1U);
        assert(!summary.steps.empty());
        assert(summary.steps.front().phase == "warmup");
        assert(summary.steps.back().phase == "measure");
        assert(summary.steps.back().rx_packets > 0U);

        std::ifstream steps_file("results/test_fake/steps.json");
        assert(steps_file.is_open());
    }

    {
        rxtech::BenchContext context;
        context.config = rxtech::load_default_config();
        context.config.output_dir = "results/test_unavailable";
        context.scenario = rxtech::load_scenario("smoke");
        context.backend = std::make_unique<UnavailableBackend>();
        context.mode = std::make_unique<rxtech::RxOnlyMode>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::BenchRunner runner;
        const rxtech::RunSummary summary = runner.run(context);
        assert(summary.run_status == "unavailable");
        assert(!summary.backend_available);
        assert(summary.backend_reason == "backend unavailable in test");
        assert(summary.steps.size() == 2U);
        assert(summary.steps.front().run_status == "skipped");
    }

    return 0;
}
