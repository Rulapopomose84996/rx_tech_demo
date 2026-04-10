/// Integration test: FileReplayBackend -> OwnerLoop -> CPI pipeline
///
/// Verifies that complete replay samples emit CpiOutput objects which pass
/// the CPI-level verifier in the normal path.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "rxtech/cpi_verifier.h"
#include "rxtech/file_replay_backend.h"
#include "rxtech/metrics.h"
#include "rxtech/owner_loop.h"
#include "rxtech/receive_context.h"
#include "rxtech/rx_config.h"

namespace
{
    rxtech::RxConfig make_replay_config()
    {
        rxtech::RxConfig cfg = rxtech::load_default_config();
        cfg.ingress.receiver_ipv4 = "172.20.11.100";
        cfg.ingress.allowed_source_ipv4 = "172.20.11.222";
        cfg.ingress.allowed_dest_port = 9999;
        cfg.protocol.channels_per_prt = 3U;
        cfg.protocol.packets_per_channel = 9U;
        cfg.protocol.expected_n_prt = 50U;
        cfg.protocol.dynamic_prt_enabled = false;
        cfg.protocol.max_n_prt = 100U;
        cfg.capture.capture_enabled = false;
        cfg.capture.raw_record_enabled = false;
        cfg.runtime.max_burst = 64U;
        return cfg;
    }

    class ExhaustDetectBackend final : public rxtech::IRxBackend
    {
      public:
        ExhaustDetectBackend(rxtech::FileReplayOptions opts, std::atomic<bool> &stop_flag)
            : inner_(std::move(opts)), stop_flag_(stop_flag)
        {
        }

        std::string name() const override
        {
            return inner_.name();
        }
        rxtech::BackendInitResult init(const rxtech::RxConfig &cfg) override
        {
            return inner_.init(cfg);
        }

        bool recv_burst(rxtech::UdpDatagramBurst &burst, std::uint32_t max) override
        {
            const bool ok = inner_.recv_burst(burst, max);
            if (ok && burst.datagrams.empty())
            {
                if (++empty_streak_ >= 3)
                {
                    stop_flag_.store(true, std::memory_order_relaxed);
                }
            }
            else
            {
                empty_streak_ = 0;
            }
            return ok;
        }

        void release_burst(rxtech::UdpDatagramBurst &burst) override
        {
            inner_.release_burst(burst);
        }
        rxtech::BackendStats stats() const override
        {
            return inner_.stats();
        }
        void shutdown() override
        {
            inner_.shutdown();
        }

      private:
        rxtech::FileReplayBackend inner_;
        std::atomic<bool> &stop_flag_;
        int empty_streak_ = 0;
    };

    struct ScenarioResult
    {
        std::vector<rxtech::CpiDecision> decisions;
        std::vector<rxtech::CpiVerificationResult> verifications;
    };

    ScenarioResult run_scenario(const std::vector<std::string> &data_dirs)
    {
        ScenarioResult result;
        std::mutex mu;
        std::atomic<bool> stop{false};

        rxtech::FileReplayOptions opts;
        opts.data_dirs = data_dirs;
        opts.loop_count = 1;
        opts.pps = 0;

        rxtech::ReceiveContext ctx;
        ctx.config = make_replay_config();
        ctx.backend = std::make_unique<ExhaustDetectBackend>(std::move(opts), stop);
        ctx.metrics = std::make_unique<rxtech::MetricsCollector>();

        const rxtech::BackendInitResult init_result = ctx.backend->init(ctx.config);
        assert(init_result.available);
        assert(init_result.ok);

        std::ostringstream cap_pkt;
        std::ostringstream cap_idx;
        rxtech::CaptureArtifacts artifacts;
        artifacts.packet_stream = &cap_pkt;
        artifacts.index_stream = &cap_idx;

        const rxtech::ProtocolSpec spec = rxtech::protocol_spec_from_config(ctx.config);
        const rxtech::CpiVerifier verifier;

        rxtech::OwnerLoop loop;
        loop.set_output_handler(
            [&](const rxtech::CpiOutput &out)
            {
                std::lock_guard<std::mutex> lk(mu);
                result.decisions.push_back(out.decision);
                result.verifications.push_back(verifier.verify(out, spec));
            });

        loop.run(ctx, artifacts, [&stop]() { return stop.load(std::memory_order_relaxed); });

        return result;
    }

} // namespace

int main()
{
    {
        const auto result = run_scenario({"data/cpi_0001_complete"});
        assert(!result.decisions.empty());
        assert(result.decisions.size() == result.verifications.size());
        for (const auto &verification : result.verifications)
        {
            assert(verification.passed);
        }
    }

    {
        const auto result = run_scenario({"data/cpi_0002_complete"});
        assert(!result.decisions.empty());
        assert(result.decisions.size() == result.verifications.size());
        for (const auto &verification : result.verifications)
        {
            assert(verification.passed);
        }
    }

    {
        const auto result = run_scenario({"data/cpi_0001_complete", "data/cpi_0002_complete"});
        assert(result.decisions.size() >= 2U);
        for (const auto &verification : result.verifications)
        {
            assert(verification.passed);
        }
    }

    return 0;
}
