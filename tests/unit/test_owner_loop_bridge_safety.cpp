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
    std::vector<std::uint8_t> make_sample_payload()
    {
        std::vector<std::uint8_t> bytes = {0x03, 0xff, 0xaa, 0x55, 0x01, 0x00, 0x00, 0x00,
                                           0x22, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
        bytes.resize(2048U, 0xABU);
        return bytes;
    }

    class MissingRawFrameBackend final : public rxtech::IRxBackend
    {
      public:
        std::string name() const override
        {
            return "missing_raw_frame";
        }

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
                payload_ = make_sample_payload();
                rxtech::UdpDatagramDesc datagram;
                datagram.payload_data = payload_.data();
                datagram.payload_len = static_cast<std::uint32_t>(payload_.size());
                datagram.src_ipv4_be = 0x7F000001U;
                datagram.dst_ipv4_be = 0x7F000001U;
                datagram.src_port = 40000U;
                datagram.dst_port = 9999U;
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
    context.config.capture.capture_enabled = false;
    context.config.capture.raw_record_enabled = false;
    context.config.runtime.duration_seconds = 1U;
    context.backend = std::make_unique<MissingRawFrameBackend>();
    context.metrics = std::make_unique<rxtech::MetricsCollector>();

    std::ostringstream packet_sink;
    std::ostringstream index_sink;
    rxtech::CaptureArtifacts artifacts;
    artifacts.packet_stream = &packet_sink;
    artifacts.index_stream = &index_sink;

    rxtech::OwnerLoop owner_loop;
    const auto *backend = static_cast<const MissingRawFrameBackend *>(context.backend.get());
    const rxtech::RunSummary summary =
        owner_loop.run(context, artifacts, [backend]() { return backend->stop_flag_.load(std::memory_order_relaxed); });

    assert(summary.run.status == "success");
    assert(summary.run.error_message.empty());
    assert(summary.protocol.rx_packets == 1U);
    assert(summary.capture.captured_packets == 1U);
    assert(summary.capture.recorded_packets == 1U);
    assert(summary.capture.captured_bytes == 2048U);
    assert(summary.capture.recorded_bytes == 2048U);
    assert(packet_sink.str().size() == 2048U);
    assert(index_sink.str().find("data_packet") != std::string::npos);
    assert(backend->polls_ == 3U);
    assert(backend->release_calls_ == 3U);
    return 0;
}
