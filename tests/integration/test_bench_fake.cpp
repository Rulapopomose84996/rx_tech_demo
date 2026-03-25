#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#ifdef __linux__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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

    {
        rxtech::BenchContext context;
        context.config = rxtech::load_default_config();
        context.config.output_dir = "results/test_until_stopped";
        context.config.run_until_stopped = true;
        context.config.status_interval_seconds = 1;
#ifdef __linux__
        context.config.feedback_enabled = true;
        context.config.feedback_host = "127.0.0.1";
        context.config.feedback_port = 19099;
        const int feedback_fd = socket(AF_INET, SOCK_DGRAM, 0);
        assert(feedback_fd >= 0);
        sockaddr_in feedback_addr{};
        feedback_addr.sin_family = AF_INET;
        feedback_addr.sin_port = htons(19099);
        inet_pton(AF_INET, "127.0.0.1", &feedback_addr.sin_addr);
        assert(bind(feedback_fd, reinterpret_cast<const sockaddr*>(&feedback_addr), sizeof(feedback_addr)) == 0);
#endif
        context.scenario.scenario_name = "manual_mode";
        context.scenario.steps = {{"measure", "measure", "steady", "fixed", 0.0, 1.0, 1U, 1U, 128U, 0U}};
        context.backend = std::make_unique<FakeBackend>();
        context.mode = std::make_unique<rxtech::RxOnlyMode>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();
        std::ostringstream status_stream;

        std::thread stopper([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            rxtech::request_bench_stop();
        });

        rxtech::BenchRunner runner;
        runner.set_status_output(&status_stream);
        const rxtech::RunSummary summary = runner.run(context);
        stopper.join();

        assert(summary.run_status == "success");
        assert(summary.measure_step_count == 1U);
        assert(summary.rx_packets > 0U);
        assert(!summary.steps.empty());
        assert(summary.steps.back().duration_seconds >= 1U);
        assert(status_stream.str().find("[status]") != std::string::npos);
        assert(status_stream.str().find("aggregate") != std::string::npos);
        assert(status_stream.str().find("drop_rate=") != std::string::npos);
#ifdef __linux__
        char buffer[512] = {};
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(feedback_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        const ssize_t received = recvfrom(feedback_fd, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (received <= 0) {
            return 1;
        }
        std::string payload(buffer, static_cast<std::size_t>(received));
        assert(payload.find("receiver_feedback") != std::string::npos);
        assert(payload.find("rx_packets") != std::string::npos);
        assert(payload.find("loss_rate") != std::string::npos);
        close(feedback_fd);
#endif
    }

    return 0;
}
