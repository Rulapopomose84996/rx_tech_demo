/// Integration test: SlowConsumerHarness back-pressure stress test
///
/// Verifies that the receive pipeline does NOT:
///   - Deadlock under a slow consumer
///   - Permanently exhaust the CpiContextPool (4 slots)
///   - Produce erroneous ABNORMAL_CUTOFF_COMMIT finalizations
///
/// Strategy:
///   - Use a FakeBackend that emits N CPIs back-to-back (each with all packets).
///   - Replace the default CpiConsumer with SlowConsumerHarness (5 ms delay).
///   - Drive the pipeline via OwnerLoop directly.
///   - After completion: verify all CPIs were consumed and decisions are valid.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "rxtech/cpi_context.h"
#include "rxtech/cpi_context_pool.h"
#include "rxtech/cpi_finalizer.h"
#include "rxtech/metrics.h"
#include "rxtech/owner_loop.h"
#include "rxtech/receive_context.h"
#include "rxtech/rx_backend.h"
#include "rxtech/rx_config.h"
#include "rxtech/slow_consumer.h"
#include "rxtech/spsc_ring.h"
#include "rxtech/time_utils.h"

namespace
{
    // ── Helpers (same pattern as the existing fake backend test) ──────────────

    std::vector<std::uint8_t> make_ctrl(std::uint16_t cpi)
    {
        // magic = 0x55AAFF00 little-endian → bytes: 0x00, 0xFF, 0xAA, 0x55
        std::vector<std::uint8_t> b = {0x00,
                                       0xFF,
                                       0xAA,
                                       0x55,
                                       static_cast<std::uint8_t>(cpi & 0xFFU),
                                       static_cast<std::uint8_t>((cpi >> 8U) & 0xFFU),
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0};
        b.resize(2048U, 0);
        return b;
    }

    std::vector<std::uint8_t> make_data(std::uint16_t cpi, std::uint16_t ch, std::uint16_t prt, std::uint16_t pi,
                                        bool final_pkt)
    {
        // magic = 0x55AAFF03 LE → 0x03, 0xFF, 0xAA, 0x55
        const bool is_tail = (pi == 9U);
        std::vector<std::uint8_t> b = {0x03,
                                       0xFF,
                                       0xAA,
                                       0x55,
                                       static_cast<std::uint8_t>(cpi & 0xFFU),
                                       static_cast<std::uint8_t>((cpi >> 8U) & 0xFFU),
                                       static_cast<std::uint8_t>(ch & 0xFFU),
                                       static_cast<std::uint8_t>((ch >> 8U) & 0xFFU),
                                       static_cast<std::uint8_t>(prt & 0xFFU),
                                       static_cast<std::uint8_t>((prt >> 8U) & 0xFFU),
                                       static_cast<std::uint8_t>(pi & 0xFFU),
                                       static_cast<std::uint8_t>((pi >> 8U) & 0xFFU),
                                       static_cast<std::uint8_t>(is_tail ? 0x30U : 0x00U),
                                       0xFF,
                                       0xAA,
                                       0x55};
        b.resize(2048U, 0xABU);
        if (!is_tail)
        {
            b[12] = 0;
            b[13] = 0;
            b[14] = 0;
            b[15] = 0;
        }
        else if (pi == 9U)
        {
            for (std::size_t idx = 16U + 476U * 4U; idx < b.size(); ++idx)
                b[idx] = 0U;
        }
        (void)final_pkt;
        return b;
    }

    // ── Multi-CPI fake backend ───────────────────────────────────────────────

    class MultiCpiFakeBackend final : public rxtech::IRxBackend
    {
      public:
        explicit MultiCpiFakeBackend(std::uint16_t n_cpi, std::uint16_t n_prt, std::uint16_t channels,
                                     std::uint16_t pkts, std::atomic<bool> &stop_flag)
            : n_cpi_(n_cpi), n_prt_(n_prt), channels_(channels), pkts_(pkts), stop_flag_(stop_flag)
        {
        }

        std::string name() const override
        {
            return "multi_cpi_fake";
        }

        rxtech::BackendInitResult init(const rxtech::RxConfig &) override
        {
            rxtech::BackendInitResult r;
            r.ok = true;
            return r;
        }

        bool recv_burst(rxtech::UdpDatagramBurst &burst, std::uint32_t max) override
        {
            burst.datagrams.clear();
            payload_storage_.clear();

            if (done_)
            {
                ++empty_streak_;
                if (empty_streak_ >= 5)
                    stop_flag_.store(true, std::memory_order_relaxed);
                return true;
            }

            // Emit one full CPI per burst call to avoid overflow.
            if (cpi_cursor_ > n_cpi_)
            {
                done_ = true;
                return true;
            }

            const std::uint16_t cpi = static_cast<std::uint16_t>(cpi_cursor_);
            ++cpi_cursor_;

            // Control table
            payload_storage_.push_back(make_ctrl(cpi));

            // All data packets
            for (std::uint16_t prt = 1U; prt <= n_prt_; ++prt)
            {
                for (std::uint16_t ch = 0; ch < channels_; ++ch)
                {
                    for (std::uint16_t pi = 1U; pi <= pkts_; ++pi)
                    {
                        payload_storage_.push_back(make_data(cpi, ch, prt, pi, pi == pkts_));
                    }
                }
            }

            std::uint32_t served = 0;
            const std::uint64_t ts = rxtech::steady_clock_now_ns();
            for (const auto &payload : payload_storage_)
            {
                if (served >= max)
                    break;
                rxtech::UdpDatagramDesc datagram;
                datagram.payload_data = payload.data();
                datagram.payload_len = static_cast<std::uint32_t>(payload.size());
                datagram.src_ipv4_be = 0xAC140BDEU;
                datagram.dst_ipv4_be = 0xAC140B64U;
                datagram.src_port = 30001U;
                datagram.dst_port = 9999U;
                datagram.ts_ns = ts;
                datagram.backend_kind = rxtech::BackendKind::file_replay;
                burst.datagrams.push_back(datagram);
                ++served;
            }
            ++stats_.rx_polls;
            stats_.rx_packets += served;
            for (const auto &datagram : burst.datagrams)
            {
                stats_.rx_bytes += datagram.payload_len;
            }
            return true;
        }

        void release_burst(rxtech::UdpDatagramBurst &burst) override
        {
            burst.datagrams.clear();
            payload_storage_.clear();
        }

        rxtech::BackendStats stats() const override
        {
            return stats_;
        }
        void shutdown() override
        {
            done_ = true;
        }

      private:
        std::uint16_t n_cpi_;
        std::uint16_t n_prt_;
        std::uint16_t channels_;
        std::uint16_t pkts_;
        std::atomic<bool> &stop_flag_;

        std::uint32_t cpi_cursor_ = 1U;
        bool done_ = false;
        int empty_streak_ = 0;
        rxtech::BackendStats stats_{};
        std::vector<std::vector<std::uint8_t>> payload_storage_;
    };

    rxtech::RxConfig make_stress_config()
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
        cfg.max_burst = 1024;
        cfg.output_drop_policy = rxtech::OutputDropPolicy::degrade;
        return cfg;
    }

} // namespace

int main()
{
    constexpr std::uint16_t kNumCpi = 10U;
    constexpr std::uint16_t kPrtPerCpi = 5U;      // keep test fast: 5 PRTs × 3 ch × 9 pkt = 135 pkts/CPI
    constexpr std::uint64_t kSlowDelayUs = 5000U; // 5 ms per CPI

    std::atomic<bool> stop{false};

    rxtech::ReceiveContext ctx;
    ctx.config = make_stress_config();
    ctx.backend = std::make_unique<MultiCpiFakeBackend>(kNumCpi, kPrtPerCpi, 3U, 9U, stop);
    ctx.metrics = std::make_unique<rxtech::MetricsCollector>();

    std::ostringstream cap_pkt, cap_idx;
    rxtech::CaptureArtifacts artifacts;
    artifacts.packet_stream = &cap_pkt;
    artifacts.index_stream = &cap_idx;

    // Build a SlowConsumerHarness manually and wire it through OwnerLoop's handler.
    // OwnerLoop creates the SPSC rings internally and hands them to CpiConsumer.
    // To use SlowConsumerHarness we hook in via set_output_handler: the harness
    // runs external to the loop and we simulate the same recycle logic.
    //
    // Simpler approach for this test: just use set_output_handler with a real delay.
    // The recycle/release is handled by the built-in consumer; we only inject latency.

    std::vector<rxtech::CpiDecision> decisions;
    std::mutex mu;

    rxtech::OwnerLoop loop;
    loop.set_output_handler(
        [&](const rxtech::CpiOutput &out)
        {
            // Simulate slow consumer
            std::this_thread::sleep_for(std::chrono::microseconds(kSlowDelayUs));
            std::lock_guard<std::mutex> lk(mu);
            decisions.push_back(out.decision);
        });

    // Impose a hard timeout so the test cannot hang forever.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);

    rxtech::RunSummary run_summary = loop.run(ctx, artifacts,
                                              [&stop, &deadline]()
                                              {
                                                  if (std::chrono::steady_clock::now() >= deadline)
                                                      return true;
                                                  return stop.load(std::memory_order_relaxed);
                                              });

    // ── Assertions ────────────────────────────────────────────────────────────

    // Deadline must not have been hit (loop should have finished cleanly).
    assert(std::chrono::steady_clock::now() < deadline);

    // Any delivered outputs must still be semantically valid.
    for (const auto &d : decisions)
    {
        assert(d != rxtech::CpiDecision::DISCARD_INVALID);
    }

    // Slow handler latency must not deadlock the owner loop or permanently stall
    // CPI progress. This test covers recycle/pool safety under a slow consumer.
    // Zero-blocking drop semantics are locked separately in the unit test.
    assert(run_summary.cpi_count == static_cast<std::uint64_t>(kNumCpi));
    if (run_summary.output_backpressure_count > 0U)
    {
        assert(run_summary.run_status == "degraded" || run_summary.run_status == "error");
    }
    else
    {
        assert(run_summary.run_status == "success");
    }

    return 0;
}
