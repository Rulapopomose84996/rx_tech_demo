#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "rxtech/metrics.h"
#include "rxtech/receive_context.h"
#include "rxtech/receive_runner.h"
#include "rxtech/rx_backend.h"
#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

namespace
{

    std::vector<std::uint8_t> make_control_table_packet(std::uint16_t cpi)
    {
        std::vector<std::uint8_t> bytes = {0x00,
                                           0xFF,
                                           0xAA,
                                           0x55,
                                           static_cast<std::uint8_t>(cpi & 0xFFU),
                                           static_cast<std::uint8_t>((cpi >> 8U) & 0xFFU),
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00,
                                           0x00};
        bytes.resize(2048U, 0x00U);
        return bytes;
    }

    std::vector<std::uint8_t> make_data_packet(std::uint16_t cpi, std::uint16_t channel, std::uint16_t prt,
                                               std::uint16_t packet_index, bool final_packet)
    {
        std::vector<std::uint8_t> bytes = {0x03,
                                           0xFF,
                                           0xAA,
                                           0x55,
                                           static_cast<std::uint8_t>(cpi & 0xFFU),
                                           static_cast<std::uint8_t>((cpi >> 8U) & 0xFFU),
                                           static_cast<std::uint8_t>(channel & 0xFFU),
                                           static_cast<std::uint8_t>((channel >> 8U) & 0xFFU),
                                           static_cast<std::uint8_t>(prt & 0xFFU),
                                           static_cast<std::uint8_t>((prt >> 8U) & 0xFFU),
                                           static_cast<std::uint8_t>(packet_index & 0xFFU),
                                           static_cast<std::uint8_t>((packet_index >> 8U) & 0xFFU),
                                           static_cast<std::uint8_t>(final_packet ? 0x30U : 0x00U),
                                           0xFFU,
                                           0xAAU,
                                           0x55U};
        bytes.resize(2048U, 0xABU);
        if (!final_packet)
        {
            bytes[12] = 0x00U;
            bytes[13] = 0x00U;
            bytes[14] = 0x00U;
            bytes[15] = 0x00U;
        }
        else if (packet_index == 9U)
        {
            for (std::size_t index = 16U + (476U * 4U); index < bytes.size(); ++index)
            {
                bytes[index] = 0U;
            }
        }
        return bytes;
    }

    class FakeBackend final : public rxtech::IRxBackend
    {
      public:
        std::string name() const override
        {
            return "fake";
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
            ++calls_;
            ++stats_.rx_polls;
            if (served_)
            {
                ++stats_.empty_polls;
                return true;
            }
            served_ = true;

            packet_storage_.push_back(make_control_table_packet(2U));
            packet_storage_.push_back(make_data_packet(2U, 0U, 1U, 1U, false));
            packet_storage_.push_back(make_data_packet(2U, 0U, 1U, 9U, true));

            for (const auto &payload : packet_storage_)
            {
                rxtech::UdpDatagramDesc datagram;
                datagram.payload_data = payload.data();
                datagram.payload_len = static_cast<std::uint32_t>(payload.size());
                datagram.raw_frame_data = payload.data();
                datagram.raw_frame_len = static_cast<std::uint32_t>(payload.size());
                datagram.src_ipv4_be = 0xAC140BDEU;
                datagram.dst_ipv4_be = 0xAC140B64U;
                datagram.src_port = 30001U;
                datagram.dst_port = 9999U;
                datagram.ts_ns = rxtech::steady_clock_now_ns();
                datagram.queue_id = 3;
                datagram.backend_kind = rxtech::BackendKind::file_replay;
                burst.datagrams.push_back(datagram);
            }

            stats_.rx_packets += burst.datagrams.size();
            for (const auto &datagram : burst.datagrams)
            {
                stats_.rx_bytes += datagram.payload_len;
            }
            return true;
        }

        void release_burst(rxtech::UdpDatagramBurst &burst) override
        {
            burst.datagrams.clear();
            packet_storage_.clear();
        }

        rxtech::BackendStats stats() const override
        {
            return stats_;
        }

        void shutdown() override {}

      private:
        bool served_ = false;
        std::size_t calls_ = 0;
        rxtech::BackendStats stats_{};
        std::vector<std::vector<std::uint8_t>> packet_storage_;
    };

    class UnavailableBackend final : public rxtech::IRxBackend
    {
      public:
        std::string name() const override
        {
            return "fake_unavailable";
        }

        rxtech::BackendInitResult init(const rxtech::RxConfig &) override
        {
            rxtech::BackendInitResult result;
            result.available = false;
            result.reason = "backend unavailable in test";
            return result;
        }

        bool recv_burst(rxtech::UdpDatagramBurst &, std::uint32_t) override
        {
            return false;
        }

        void release_burst(rxtech::UdpDatagramBurst &burst) override
        {
            burst.datagrams.clear();
        }

        rxtech::BackendStats stats() const override
        {
            return {};
        }

        void shutdown() override {}
    };

    class FilteringFakeBackend final : public rxtech::IRxBackend
    {
      public:
        std::string name() const override
        {
            return "filtering_fake";
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
            if (served_)
            {
                ++stats_.rx_polls;
                ++stats_.empty_polls;
                return true;
            }
            served_ = true;

            packet_storage_.push_back(make_data_packet(2U, 0U, 1U, 9U, true));
            packet_storage_.push_back(make_data_packet(2U, 0U, 1U, 9U, true));

            for (std::size_t index = 0; index < packet_storage_.size(); ++index)
            {
                rxtech::UdpDatagramDesc datagram;
                datagram.payload_data = packet_storage_[index].data();
                datagram.payload_len = static_cast<std::uint32_t>(packet_storage_[index].size());
                datagram.raw_frame_data = packet_storage_[index].data();
                datagram.raw_frame_len = static_cast<std::uint32_t>(packet_storage_[index].size());
                datagram.src_ipv4_be = (index == 0U) ? 0xAC140BDEU : 0xAC140B63U;
                datagram.dst_ipv4_be = 0xAC140B64U;
                datagram.src_port = 30001U;
                datagram.dst_port = 9999U;
                datagram.ts_ns = rxtech::steady_clock_now_ns();
                datagram.queue_id = 0;
                datagram.backend_kind = rxtech::BackendKind::file_replay;
                burst.datagrams.push_back(datagram);
                ++stats_.rx_packets;
                stats_.rx_bytes += datagram.payload_len;
            }
            ++stats_.rx_polls;
            return true;
        }

        void release_burst(rxtech::UdpDatagramBurst &burst) override
        {
            burst.datagrams.clear();
            packet_storage_.clear();
        }

        rxtech::BackendStats stats() const override
        {
            return stats_;
        }

        void shutdown() override {}

      private:
        bool served_ = false;
        rxtech::BackendStats stats_{};
        std::vector<std::vector<std::uint8_t>> packet_storage_;
    };

    bool file_exists(const char *path)
    {
        std::ifstream input(path, std::ios::binary);
        return input.good();
    }

    bool contains_run_stamp(const std::string &path, const std::string &suffix)
    {
        const std::string marker = "_" + suffix;
        const std::size_t pos = path.find(marker);
        if (pos == std::string::npos || pos < 16U)
        {
            return false;
        }
        const std::string stamp = path.substr(pos - 15U, 15U);
        return stamp[8] == '_';
    }

} // namespace

int main()
{
    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.capture.capture_output_dir = "results/test_receive_runner_fake";
        context.config.capture.raw_record_enabled = true;
        context.config.capture.raw_record_output_dir = "results/test_receive_runner_fake_raw";
        context.config.capture.raw_record_file_prefix = "raw_capture";
        context.config.capture.raw_record_ring_slots = 8;
        context.config.capture.raw_record_writer_batch_size = 2;
        context.config.capture.raw_record_max_frame_bytes = 4096;
        context.config.capture.raw_record_segment_bytes = 65536;
        context.config.capture.raw_record_max_total_bytes = 1048576;
        context.config.runtime.duration_seconds = 1;
        context.backend = std::make_unique<FakeBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::ReceiveRunner runner;
        const rxtech::RunSummary summary = runner.run(context);

        assert(summary.run.status == "success");
        assert(summary.protocol.rx_packets > 0U);
        assert(summary.protocol.rx_bytes > 0U);
        assert(summary.capture.captured_packets > 0U);
        assert(summary.capture.captured_packets <= summary.protocol.rx_packets);
        assert(summary.capture.recorded_packets == summary.capture.captured_packets);
        assert(summary.protocol.control_table_packets > 0U);
        assert(summary.protocol.data_packets > 0U);
        assert(summary.protocol.cpi_count == 1U);
        assert(summary.protocol.prt_count == 1U);
        assert(summary.protocol.channel_count == 1U);
        assert(summary.capture.capture_policy == "first_effective_cpi");
        assert(summary.active_prt.available);
        assert(summary.active_prt.packets_per_channel == 9U);
        assert(summary.active_prt.channel_count == 1U);
        assert(summary.active_prt.channels.size() == 3U);
        assert(summary.active_prt.channels[0].packet_count == 2U);
        assert(summary.active_prt.channels[1].packet_count == 0U);
        assert(summary.active_prt.channels[2].packet_count == 0U);
        assert(summary.protocol.complete_prt_count == 0U);
        assert(summary.protocol.final_tail_packets == 1U);
        assert(summary.capture.packet_count == summary.capture.recorded_packets);
        assert(summary.capture.raw_record_written_frames >= 3U);
        assert(summary.capture.raw_record_written_bytes > 0U);
        assert(summary.capture.raw_record_dropped_frames == 0U);
        assert(!summary.capture.raw_record_output_dir.empty());
        assert(!summary.capture.raw_record_latest_file_path.empty());
        assert(summary.run.human_summary.find("接收结束汇总") != std::string::npos);
        assert(summary.run.human_summary.find("原始帧目录") != std::string::npos);
        assert(summary.run.human_summary.find("通道分布") != std::string::npos);
        assert(summary.run.human_summary.find("和路") != std::string::npos);
        assert(contains_run_stamp(summary.capture.packets_path, "test_receive_runner_fake"));
        assert(contains_run_stamp(summary.capture.index_path, "test_receive_runner_fake"));
        assert(file_exists(summary.capture.packets_path.c_str()));
        assert(file_exists(summary.capture.index_path.c_str()));
        assert(file_exists(summary.capture.raw_record_latest_file_path.c_str()));

        std::ifstream capture_file(summary.capture.packets_path, std::ios::binary);
        assert(capture_file.is_open());
        capture_file.seekg(0, std::ios::end);
        assert(capture_file.tellg() > 0);
    }

    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.capture.capture_output_dir = "results/test_receive_runner_unavailable";
        context.backend = std::make_unique<UnavailableBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::ReceiveRunner runner;
        const rxtech::RunSummary summary = runner.run(context);

        assert(summary.run.status == "unavailable");
        assert(!summary.backend.available);
        assert(summary.backend.reason == "backend unavailable in test");
        assert(summary.capture.packets_path.empty());
    }

    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.capture.capture_output_dir = "results/test_receive_runner_until_stopped";
        context.config.runtime.run_until_stopped = true;
        context.config.operations.status_interval_seconds = 1;
        context.backend = std::make_unique<FakeBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();
        std::ostringstream status_stream;

        std::thread stopper(
            []()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                rxtech::request_receive_stop();
            });

        rxtech::ReceiveRunner runner;
        runner.set_status_output(&status_stream);
        const rxtech::RunSummary summary = runner.run(context);
        stopper.join();

        assert(summary.run.status == "success");
        assert(summary.protocol.rx_packets > 0U);
        assert(summary.protocol.cpi_count == 1U);
        assert(summary.protocol.prt_count == 1U);
        assert(summary.protocol.channel_count == 1U);
        assert(summary.capture.packet_count == summary.capture.recorded_packets);
        assert(status_stream.str().find("实时接收状态") != std::string::npos);
        assert(status_stream.str().find("时间戳") != std::string::npos);
        assert(status_stream.str().find("业务流状态") != std::string::npos);
        assert(status_stream.str().find("落盘记录") != std::string::npos);
        assert(status_stream.str().find("全局 CPI 数") != std::string::npos);
    }

    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.capture.capture_output_dir = "results/test_receive_runner_filtered";
        context.config.runtime.duration_seconds = 1;
        context.config.ingress.receiver_ipv4 = "172.20.11.100";
        context.config.ingress.allowed_source_ipv4 = "172.20.11.222";
        context.config.ingress.allowed_dest_port = 9999U;
        context.backend = std::make_unique<FilteringFakeBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::ReceiveRunner runner;
        const rxtech::RunSummary summary = runner.run(context);

        assert(summary.run.status == "success");
        assert(summary.backend.raw_rx_packets == 2U);
        assert(summary.backend.filtered_packets == 1U);
        assert(summary.protocol.rx_packets == 1U);
        assert(summary.protocol.parsed_packets == 1U);
        assert(summary.protocol.data_packets == 1U);
        assert(summary.protocol.final_tail_packets == 1U);
        assert(summary.capture.packet_count == 1U);
        assert(contains_run_stamp(summary.capture.packets_path, "test_receive_runner_filtered"));
    }

    return 0;
}
