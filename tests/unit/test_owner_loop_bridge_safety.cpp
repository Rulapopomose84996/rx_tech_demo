#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "rxtech/metrics.h"
#include "rxtech/owner_loop.h"
#include "rxtech/receive_context.h"
#include "rxtech/rx_backend.h"
#include "rxtech/rx_config.h"

namespace
{

    class MissingRawFrameBackend final : public rxtech::IRxBackend
    {
    public:
        std::string name() const override { return "missing_raw_frame"; }

        rxtech::BackendInitResult init(const rxtech::RxConfig &) override
        {
            rxtech::BackendInitResult result;
            result.ok = true;
            return result;
        }

        bool recv_burst(rxtech::UdpDatagramBurst &burst, std::uint32_t) override
        {
            burst.datagrams.clear();
            ++polls_;
            if (polls_ == 1U)
            {
                payload_.assign({0x03U, 0xFFU, 0xAAU, 0x55U});
                rxtech::UdpDatagramDesc datagram;
                datagram.payload_data = payload_.data();
                datagram.payload_len = static_cast<std::uint32_t>(payload_.size());
                datagram.backend_kind = rxtech::BackendKind::socket;
                burst.datagrams.push_back(datagram);
            }
            else if (polls_ >= 3U)
            {
                stop_flag_.store(true, std::memory_order_relaxed);
            }
            return true;
        }

        void release_burst(rxtech::UdpDatagramBurst &burst) override
        {
            ++release_calls_;
            burst.datagrams.clear();
        }

        rxtech::BackendStats stats() const override
        {
            return stats_;
        }

        void shutdown() override {}

        std::atomic<bool> stop_flag_{false};
        std::uint32_t polls_ = 0;
        std::uint32_t release_calls_ = 0;

    private:
        rxtech::BackendStats stats_{};
        std::vector<std::uint8_t> payload_;
    };

} // namespace

int main()
{
    rxtech::ReceiveContext context;
    context.config = rxtech::load_default_config();
    context.config.capture_enabled = false;
    context.config.raw_record_enabled = false;
    context.config.duration_seconds = 1U;
    context.backend = std::make_unique<MissingRawFrameBackend>();
    context.metrics = std::make_unique<rxtech::MetricsCollector>();

    std::ostringstream packet_sink;
    std::ostringstream index_sink;
    rxtech::CaptureArtifacts artifacts;
    artifacts.packet_stream = &packet_sink;
    artifacts.index_stream = &index_sink;

    rxtech::OwnerLoop owner_loop;
    const auto *backend = static_cast<const MissingRawFrameBackend *>(context.backend.get());
    const rxtech::RunSummary summary = owner_loop.run(
        context,
        artifacts,
        [backend]()
        {
            return backend->stop_flag_.load(std::memory_order_relaxed);
        });

    assert(summary.run_status == "error");
    assert(summary.error_message.find("raw_frame_data/raw_frame_len") != std::string::npos);
    assert(backend->polls_ == 1U);
    assert(backend->release_calls_ == 1U);
    return 0;
}
