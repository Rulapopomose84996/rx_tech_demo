/// Integration test: FileReplayBackend → OwnerLoop → CPI pipeline
///
/// Feeds cpi_0001 and cpi_0002 binary samples through the standard receive
/// pipeline (via OwnerLoop) and verifies each produces a CpiOutput with
/// decision COMPLETE_OK.
///
/// Requirements:
///   - data/cpi_0001_complete/ and data/cpi_0002_complete/ present in CWD
///   - No DPDK required (FileReplayBackend uses file I/O only)
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "rxtech/cpi_context.h"
#include "rxtech/cpi_finalizer.h"
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
        cfg.receiver_ipv4 = "172.20.11.100";
        cfg.allowed_source_ipv4 = "172.20.11.222";
        cfg.allowed_dest_port = 9999;
        cfg.protocol_channels_per_prt = 3;
        cfg.protocol_packets_per_channel = 9;
        cfg.protocol_dynamic_prt_enabled = true;
        cfg.protocol_max_n_prt = 100;
        cfg.capture_enabled = false;
        cfg.raw_record_enabled = false;
        cfg.max_burst = 64;
        return cfg;
    }

    /// Thin backend wrapper: forwards to FileReplayBackend and sets a stop flag
    /// after three consecutive empty bursts (i.e. all data has been served).
    class ExhaustDetectBackend final : public rxtech::IRxBackend
    {
    public:
        ExhaustDetectBackend(rxtech::FileReplayOptions opts, std::atomic<bool> &stop_flag)
            : inner_(std::move(opts)), stop_flag_(stop_flag)
        {
        }

        std::string name() const override { return inner_.name(); }

        rxtech::BackendInitResult init(const rxtech::RxConfig &cfg) override
        {
            return inner_.init(cfg);
        }

        bool recv_burst(rxtech::RxBurst &burst, std::uint32_t max) override
        {
            const bool ok = inner_.recv_burst(burst, max);
            if (ok && burst.packets.empty())
            {
                if (++empty_streak_ >= 3)
                    stop_flag_.store(true, std::memory_order_relaxed);
            }
            else
            {
                empty_streak_ = 0;
            }
            return ok;
        }

        void release_burst(rxtech::RxBurst &burst) override { inner_.release_burst(burst); }
        rxtech::BackendStats stats() const override { return inner_.stats(); }
        void shutdown() override { inner_.shutdown(); }

    private:
        rxtech::FileReplayBackend inner_;
        std::atomic<bool> &stop_flag_;
        int empty_streak_ = 0;
    };

    /// Run a replay scenario and return the CpiDecision for every CpiOutput emitted.
    std::vector<rxtech::CpiDecision> run_scenario(const std::vector<std::string> &data_dirs)
    {
        std::vector<rxtech::CpiDecision> decisions;
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

        // CaptureSink always needs valid streams even when capture is disabled.
        std::ostringstream cap_pkt;
        std::ostringstream cap_idx;
        rxtech::CaptureArtifacts artifacts;
        artifacts.packet_stream = &cap_pkt;
        artifacts.index_stream = &cap_idx;

        rxtech::OwnerLoop loop;
        loop.set_output_handler([&](const rxtech::CpiOutput &out)
                                {
            std::lock_guard<std::mutex> lk(mu);
            decisions.push_back(out.decision); });

        loop.run(ctx, artifacts, [&stop]()
                 { return stop.load(std::memory_order_relaxed); });

        return decisions;
    }

} // namespace

int main()
{
    // ── Test 1: cpi_0001 ─────────────────────────────────────────────────────
    {
        const auto decisions = run_scenario({"data/cpi_0001_complete"});
        assert(!decisions.empty());
        for (const auto &d : decisions)
        {
            assert(d == rxtech::CpiDecision::COMPLETE_OK);
        }
    }

    // ── Test 2: cpi_0002 ─────────────────────────────────────────────────────
    {
        const auto decisions = run_scenario({"data/cpi_0002_complete"});
        assert(!decisions.empty());
        for (const auto &d : decisions)
        {
            assert(d == rxtech::CpiDecision::COMPLETE_OK);
        }
    }

    // ── Test 3: cpi_0001 followed by cpi_0002 in one sequence ────────────────
    {
        const auto decisions = run_scenario({"data/cpi_0001_complete", "data/cpi_0002_complete"});
        assert(decisions.size() >= 2U);
        for (const auto &d : decisions)
        {
            assert(d == rxtech::CpiDecision::COMPLETE_OK);
        }
    }

    return 0;
}

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "rxtech/cpi_context.h"
#include "rxtech/cpi_finalizer.h"
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
        cfg.receiver_ipv4 = "172.20.11.100";
        cfg.allowed_source_ipv4 = "172.20.11.222";
        cfg.allowed_dest_port = 9999;
        cfg.protocol_channels_per_prt = 3;
        cfg.protocol_packets_per_channel = 9;
        cfg.protocol_dynamic_prt_enabled = true;
        cfg.protocol_max_n_prt = 100;
        cfg.duration_seconds = 60; // generous; backend signals done by returning stop
        cfg.capture_enabled = false;
        cfg.raw_record_enabled = false;
        cfg.max_burst = 64;
        return cfg;
    }

    /// Run a single replay scenario through OwnerLoop and return collected decisions.
    std::vector<rxtech::CpiDecision> run_replay_scenario(
        std::vector<std::string> data_dirs,
        rxtech::RunSummary *out_summary = nullptr)
    {
        std::vector<rxtech::CpiDecision> decisions;
        std::mutex mu;

        rxtech::FileReplayOptions opts;
        opts.data_dirs = std::move(data_dirs);
        opts.loop_count = 1;
        opts.pps = 0; // as fast as possible

        rxtech::ReceiveContext ctx;
        ctx.config = make_replay_config();
        ctx.backend = std::make_unique<rxtech::FileReplayBackend>(std::move(opts));
        ctx.metrics = std::make_unique<rxtech::MetricsCollector>();

        // Null sinks for capture streams (capture_enabled = false, but CaptureSink
        // still needs valid streams — use in-memory sinks).
        std::ostringstream cap_pkt_sink;
        std::ostringstream cap_idx_sink;

        rxtech::CaptureArtifacts artifacts;
        artifacts.packet_stream = &cap_pkt_sink;
        artifacts.index_stream = &cap_idx_sink;
        artifacts.raw_frame_recorder = nullptr;

        rxtech::OwnerLoop loop;
        loop.set_output_handler([&](const rxtech::CpiOutput &out)
                                {
            std::lock_guard<std::mutex> lock(mu);
            decisions.push_back(out.decision); });

        // The backend signals "done" by returning true with an empty burst after
        // all frames are served.  We rely on that: should_stop just checks whether
        // the backend has gone idle for a while, which the backend already handles
        // by setting its stopped flag.  Drive termination via duration + backend
        // exhaustion: the backend returns true with 0 packets once all frames are
        // served; the owner_loop then checks should_stop each iteration.
        std::atomic<bool> backend_done{false};
        const auto should_stop = [&]() -> bool
        {
            return backend_done.load(std::memory_order_relaxed);
        };

        // Wrap the backend to detect when it is exhausted.
        struct VanishingBackend : public rxtech::IRxBackend
        {
            rxtech::FileReplayBackend inner;
            std::atomic<bool> &done;
            int empty_streak = 0;

            VanishingBackend(rxtech::FileReplayOptions o, std::atomic<bool> &d)
                : inner(std::move(o)), done(d) {}

            std::string name() const override { return inner.name(); }
            rxtech::BackendInitResult init(const rxtech::RxConfig &c) override { return inner.init(c); }
            void release_burst(rxtech::RxBurst &b) override { inner.release_burst(b); }
            rxtech::BackendStats stats() const override { return inner.stats(); }
            void shutdown() override { inner.shutdown(); }

            bool recv_burst(rxtech::RxBurst &burst, std::uint32_t max) override
            {
                const bool ok = inner.recv_burst(burst, max);
                if (ok && burst.packets.empty())
                {
                    ++empty_streak;
                    if (empty_streak >= 3)
                        done.store(true, std::memory_order_relaxed);
                }
                else
                {
                    empty_streak = 0;
                }
                return ok;
            }
        };

        rxtech::FileReplayOptions opts2;
        opts2.data_dirs = ctx.config.capture_enabled ? std::vector<std::string>{} : std::vector<std::string>{}; // unused

        // Move the real backend into VanishingBackend
        auto *file_be = dynamic_cast<rxtech::FileReplayBackend *>(ctx.backend.get());
        (void)file_be;
        // Re-create cleanly because we moved opts above
        ctx.backend.reset();

        rxtech::FileReplayOptions opts_clean;
        opts_clean.data_dirs = std::move(opts.data_dirs); // already moved — re-supply via caller

        // Simplest approach: run with ctx.backend = the FileReplayBackend and
        // stop via a fixed iteration count detected through an empty-burst streak.
        // Restore the backend we already created.
        // (opts.data_dirs was moved to ctx; rebuild from the original parameter)
        // NOTE: The lambda captures backend_done; we just need to restart it.
        //
        // ← Re-approach: since the move semantics got complicated above,
        //   simplify: use struct directly in ctx.backend.
        ctx.backend = []() -> rxtech::BackendPtr
        {
            return {}; // placeholder - see below
        }();

        return decisions; // placeholder — see complete rewrite below
    }

} // namespace

// ── Full clean implementation ─────────────────────────────────────────────────

namespace
{
    /// RAII wrapper: runs backend + OwnerLoop, stopping after N consecutive
    /// empty bursts from the backend (meaning all data is exhausted).
    struct ReplayScenario
    {
        std::vector<rxtech::CpiDecision> decisions;
        rxtech::RunSummary summary;
        std::mutex mu;

        void run(const std::vector<std::string> &data_dirs)
        {
            rxtech::FileReplayOptions opts;
            opts.data_dirs = data_dirs;
            opts.loop_count = 1;
            opts.pps = 0;

            rxtech::ReceiveContext ctx;
            ctx.config = make_replay_config();
            ctx.backend = std::make_unique<rxtech::FileReplayBackend>(opts);
            ctx.metrics = std::make_unique<rxtech::MetricsCollector>();

            std::ostringstream cap_pkt;
            std::ostringstream cap_idx;
            rxtech::CaptureArtifacts artifacts;
            artifacts.packet_stream = &cap_pkt;
            artifacts.index_stream = &cap_idx;

            rxtech::OwnerLoop loop;
            loop.set_output_handler([this](const rxtech::CpiOutput &out)
                                    {
                std::lock_guard<std::mutex> lk(mu);
                decisions.push_back(out.decision); });

            std::atomic<bool> done{false};
            summary = loop.run(ctx, artifacts, [&done]()
                               { return done.load(std::memory_order_relaxed); });
            (void)summary;
        }
    };

} // namespace

// The issue above is: OwnerLoop::run blocks until should_stop() returns true,
// but FileReplayBackend stops serving packets and just returns empty bursts
// forever.  We need to detect that and signal stop.
//
// Solution: wrap FileReplayBackend in a thin shim that signals stop after
// a few consecutive empty bursts.

namespace
{
    class ExhaustDetectBackend final : public rxtech::IRxBackend
    {
    public:
        ExhaustDetectBackend(rxtech::FileReplayOptions opts, std::atomic<bool> &stop_flag)
            : inner_(std::move(opts)), stop_flag_(stop_flag) {}

        std::string name() const override { return inner_.name(); }

        rxtech::BackendInitResult init(const rxtech::RxConfig &cfg) override
        {
            return inner_.init(cfg);
        }

        bool recv_burst(rxtech::RxBurst &burst, std::uint32_t max) override
        {
            const bool ok = inner_.recv_burst(burst, max);
            if (ok && burst.packets.empty())
            {
                if (++empty_streak_ >= 3)
                    stop_flag_.store(true, std::memory_order_relaxed);
            }
            else
            {
                empty_streak_ = 0;
            }
            return ok;
        }

        void release_burst(rxtech::RxBurst &burst) override { inner_.release_burst(burst); }
        rxtech::BackendStats stats() const override { return inner_.stats(); }
        void shutdown() override { inner_.shutdown(); }

    private:
        rxtech::FileReplayBackend inner_;
        std::atomic<bool> &stop_flag_;
        int empty_streak_ = 0;
    };

    std::vector<rxtech::CpiDecision> run_scenario(const std::vector<std::string> &data_dirs)
    {
        std::vector<rxtech::CpiDecision> decisions;
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

        std::ostringstream cap_pkt;
        std::ostringstream cap_idx;
        rxtech::CaptureArtifacts artifacts;
        artifacts.packet_stream = &cap_pkt;
        artifacts.index_stream = &cap_idx;

        rxtech::OwnerLoop loop;
        loop.set_output_handler([&](const rxtech::CpiOutput &out)
                                {
            std::lock_guard<std::mutex> lk(mu);
            decisions.push_back(out.decision); });

        loop.run(ctx, artifacts, [&stop]()
                 { return stop.load(std::memory_order_relaxed); });

        return decisions;
    }

} // namespace

int main()
{
    // ── Test 1: cpi_0001 ─────────────────────────────────────────────────────
    {
        const auto decisions = run_scenario({"data/cpi_0001_complete"});
        assert(!decisions.empty());
        for (const auto &d : decisions)
            assert(d == rxtech::CpiDecision::COMPLETE_OK);
    }

    // ── Test 2: cpi_0002 ─────────────────────────────────────────────────────
    {
        const auto decisions = run_scenario({"data/cpi_0002_complete"});
        assert(!decisions.empty());
        for (const auto &d : decisions)
            assert(d == rxtech::CpiDecision::COMPLETE_OK);
    }

    // ── Test 3: cpi_0001 + cpi_0002 in sequence ──────────────────────────────
    {
        const auto decisions = run_scenario({"data/cpi_0001_complete", "data/cpi_0002_complete"});
        assert(decisions.size() >= 2U);
        for (const auto &d : decisions)
            assert(d == rxtech::CpiDecision::COMPLETE_OK);
    }

    return 0;
}
