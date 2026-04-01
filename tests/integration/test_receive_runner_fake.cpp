#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "rxtech/demo_protocol.h"
#include "rxtech/metrics.h"
#include "rxtech/receive_context.h"
#include "rxtech/receive_runner.h"
#include "rxtech/rx_backend.h"
#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

namespace {

std::vector<std::uint8_t> make_valid_demo_packet() {
    std::vector<std::uint8_t> bytes = {
        0x54, 0x50, 0x44, 0x58, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x00,
        0x80, 0x00, 0x00, 0x00
    };
    bytes.resize(rxtech::kDemoHeaderWireBytes + 128U, 0xABU);
    return bytes;
}

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
        packet.queue_id = 3;
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
    std::vector<std::uint8_t> payload_ = make_valid_demo_packet();
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

bool file_exists(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return input.good();
}

}  // namespace

int main() {
    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.output_dir = "results/test_receive_runner_fake";
        context.config.duration_seconds = 1;
        context.backend = std::make_unique<FakeBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::ReceiveRunner runner;
        const rxtech::RunSummary summary = runner.run(context);

        assert(summary.run_status == "success");
        assert(summary.rx_packets > 0U);
        assert(summary.rx_bytes > 0U);
        assert(summary.captured_packets == summary.rx_packets);
        assert(summary.recorded_packets == summary.rx_packets);
        assert(file_exists("results/test_receive_runner_fake/capture_packets.bin"));
        assert(file_exists("results/test_receive_runner_fake/capture_index.csv"));

        std::ifstream capture_file("results/test_receive_runner_fake/capture_packets.bin", std::ios::binary);
        assert(capture_file.is_open());
        capture_file.seekg(0, std::ios::end);
        assert(capture_file.tellg() > 0);
    }

    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.output_dir = "results/test_receive_runner_unavailable";
        context.backend = std::make_unique<UnavailableBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::ReceiveRunner runner;
        const rxtech::RunSummary summary = runner.run(context);

        assert(summary.run_status == "unavailable");
        assert(!summary.backend_available);
        assert(summary.backend_reason == "backend unavailable in test");
        assert(!file_exists("results/test_receive_runner_unavailable/capture_packets.bin"));
    }

    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.output_dir = "results/test_receive_runner_until_stopped";
        context.config.run_until_stopped = true;
        context.config.status_interval_seconds = 1;
        context.backend = std::make_unique<FakeBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();
        std::ostringstream status_stream;

        std::thread stopper([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            rxtech::request_receive_stop();
        });

        rxtech::ReceiveRunner runner;
        runner.set_status_output(&status_stream);
        const rxtech::RunSummary summary = runner.run(context);
        stopper.join();

        assert(summary.run_status == "success");
        assert(summary.rx_packets > 0U);
        assert(status_stream.str().find("[status]") != std::string::npos);
        assert(status_stream.str().find("captured_packets=") != std::string::npos);
    }

    return 0;
}
