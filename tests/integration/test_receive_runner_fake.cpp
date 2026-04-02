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
        std::vector<std::uint8_t> bytes = {
            0x00, 0xFF, 0xAA, 0x55,
            static_cast<std::uint8_t>(cpi & 0xFFU), static_cast<std::uint8_t>((cpi >> 8U) & 0xFFU),
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        bytes.resize(2048U, 0x00U);
        return bytes;
    }

    std::vector<std::uint8_t> make_data_packet(std::uint16_t cpi,
                                               std::uint16_t channel,
                                               std::uint16_t prt,
                                               std::uint16_t packet_index,
                                               bool final_packet)
    {
        std::vector<std::uint8_t> bytes = {
            0x03, 0xFF, 0xAA, 0x55,
            static_cast<std::uint8_t>(cpi & 0xFFU), static_cast<std::uint8_t>((cpi >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(channel & 0xFFU), static_cast<std::uint8_t>((channel >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(prt & 0xFFU), static_cast<std::uint8_t>((prt >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(packet_index & 0xFFU), static_cast<std::uint8_t>((packet_index >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(final_packet ? 0x30U : 0x00U), 0xFFU, 0xAAU, 0x55U};
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

    std::vector<std::uint8_t> make_udp_frame(const std::vector<std::uint8_t> &payload,
                                             std::uint32_t source_ipv4_be,
                                             std::uint32_t dest_ipv4_be,
                                             std::uint16_t source_port,
                                             std::uint16_t dest_port)
    {
        const std::uint16_t udp_length = static_cast<std::uint16_t>(8U + payload.size());
        const std::uint16_t total_length = static_cast<std::uint16_t>(20U + udp_length);
        std::vector<std::uint8_t> bytes = {
            0x9c, 0x47, 0x82, 0xe1, 0x36, 0xd0, 0x9c, 0x47, 0x82, 0xe1, 0x36, 0xdc, 0x08, 0x00,
            0x45, 0x00,
            static_cast<std::uint8_t>((total_length >> 8U) & 0xFFU), static_cast<std::uint8_t>(total_length & 0xFFU),
            0x00, 0x00, 0x00, 0x00, 0x40, 0x11, 0x00, 0x00,
            static_cast<std::uint8_t>((source_ipv4_be >> 24U) & 0xFFU),
            static_cast<std::uint8_t>((source_ipv4_be >> 16U) & 0xFFU),
            static_cast<std::uint8_t>((source_ipv4_be >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(source_ipv4_be & 0xFFU),
            static_cast<std::uint8_t>((dest_ipv4_be >> 24U) & 0xFFU),
            static_cast<std::uint8_t>((dest_ipv4_be >> 16U) & 0xFFU),
            static_cast<std::uint8_t>((dest_ipv4_be >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(dest_ipv4_be & 0xFFU),
            static_cast<std::uint8_t>((source_port >> 8U) & 0xFFU), static_cast<std::uint8_t>(source_port & 0xFFU),
            static_cast<std::uint8_t>((dest_port >> 8U) & 0xFFU), static_cast<std::uint8_t>(dest_port & 0xFFU),
            static_cast<std::uint8_t>((udp_length >> 8U) & 0xFFU), static_cast<std::uint8_t>(udp_length & 0xFFU),
            0x00, 0x00};
        bytes.insert(bytes.end(), payload.begin(), payload.end());
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

        bool recv_burst(rxtech::RxBurst &burst, std::uint32_t) override
        {
            burst.packets.clear();
            ++calls_;
            ++stats_.rx_polls;
            if (served_)
            {
                ++stats_.empty_polls;
                return true;
            }
            served_ = true;

            packet_storage_.push_back(make_udp_frame(make_control_table_packet(2U), 0xAC140BDEU, 0xAC140B64U, 30001U, 9999U));
            packet_storage_.push_back(make_udp_frame(make_data_packet(2U, 0U, 1U, 1U, false), 0xAC140BDEU, 0xAC140B64U, 30001U, 9999U));
            packet_storage_.push_back(make_udp_frame(make_data_packet(2U, 0U, 1U, 9U, true), 0xAC140BDEU, 0xAC140B64U, 30001U, 9999U));

            for (const auto &payload : packet_storage_)
            {
                rxtech::PacketDesc packet;
                packet.data = const_cast<std::uint8_t *>(payload.data());
                packet.len = static_cast<std::uint32_t>(payload.size());
                packet.ts_ns = rxtech::steady_clock_now_ns();
                packet.queue_id = 3;
                burst.packets.push_back(packet);
            }

            stats_.rx_packets += burst.packets.size();
            for (const auto &packet : burst.packets)
            {
                stats_.rx_bytes += packet.len;
            }
            return true;
        }

        void release_burst(rxtech::RxBurst &burst) override
        {
            burst.packets.clear();
            packet_storage_.clear();
        }

        rxtech::BackendStats stats() const override
        {
            return stats_;
        }

        void shutdown() override
        {
        }

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

        bool recv_burst(rxtech::RxBurst &, std::uint32_t) override
        {
            return false;
        }

        void release_burst(rxtech::RxBurst &burst) override
        {
            burst.packets.clear();
        }

        rxtech::BackendStats stats() const override
        {
            return {};
        }

        void shutdown() override
        {
        }
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

        bool recv_burst(rxtech::RxBurst &burst, std::uint32_t) override
        {
            burst.packets.clear();
            if (served_)
            {
                ++stats_.rx_polls;
                ++stats_.empty_polls;
                return true;
            }
            served_ = true;

            packet_storage_.push_back(make_udp_frame(make_data_packet(2U, 0U, 1U, 1U, true),
                                                     0xAC140BDEU,
                                                     0xAC140B64U,
                                                     30001U,
                                                     9999U));
            packet_storage_.push_back(make_udp_frame(make_data_packet(2U, 0U, 1U, 1U, true),
                                                     0xAC140B63U,
                                                     0xAC140B64U,
                                                     30001U,
                                                     9999U));

            for (const auto &payload : packet_storage_)
            {
                rxtech::PacketDesc packet;
                packet.data = const_cast<std::uint8_t *>(payload.data());
                packet.len = static_cast<std::uint32_t>(payload.size());
                packet.ts_ns = rxtech::steady_clock_now_ns();
                packet.queue_id = 0;
                burst.packets.push_back(packet);
                ++stats_.rx_packets;
                stats_.rx_bytes += packet.len;
            }
            ++stats_.rx_polls;
            return true;
        }

        void release_burst(rxtech::RxBurst &burst) override
        {
            burst.packets.clear();
            packet_storage_.clear();
        }

        rxtech::BackendStats stats() const override
        {
            return stats_;
        }

        void shutdown() override
        {
        }

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

} // namespace

int main()
{
    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.capture_output_dir = "results/test_receive_runner_fake";
        context.config.raw_record_enabled = true;
        context.config.raw_record_output_dir = "results/test_receive_runner_fake_raw";
        context.config.raw_record_file_prefix = "raw_capture";
        context.config.raw_record_ring_slots = 8;
        context.config.raw_record_writer_batch_size = 2;
        context.config.raw_record_max_frame_bytes = 4096;
        context.config.raw_record_segment_bytes = 65536;
        context.config.raw_record_max_total_bytes = 1048576;
        context.config.duration_seconds = 1;
        context.backend = std::make_unique<FakeBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::ReceiveRunner runner;
        const rxtech::RunSummary summary = runner.run(context);

        assert(summary.run_status == "success");
        assert(summary.rx_packets > 0U);
        assert(summary.rx_bytes > 0U);
        assert(summary.captured_packets > 0U);
        assert(summary.captured_packets <= summary.rx_packets);
        assert(summary.recorded_packets == summary.captured_packets);
        assert(summary.control_table_packets > 0U);
        assert(summary.data_packets > 0U);
        assert(summary.cpi_count == 1U);
        assert(summary.prt_count == 1U);
        assert(summary.channel_count == 1U);
        assert(summary.complete_prt_count == 0U);
        assert(summary.final_tail_packets == 1U);
        assert(summary.packet_count == summary.recorded_packets);
        assert(summary.raw_record_written_frames >= 3U);
        assert(summary.raw_record_written_bytes > 0U);
        assert(summary.raw_record_dropped_frames == 0U);
        assert(!summary.raw_record_output_dir.empty());
        assert(!summary.raw_record_latest_file_path.empty());
        assert(summary.human_summary.find("接收结束汇总") != std::string::npos);
        assert(summary.human_summary.find("原始帧目录") != std::string::npos);
        assert(summary.human_summary.find("通道分布") != std::string::npos);
        assert(summary.human_summary.find("和路") != std::string::npos);
        assert(file_exists("results/test_receive_runner_fake/capture_packets.bin"));
        assert(file_exists("results/test_receive_runner_fake/capture_index.csv"));
        assert(file_exists(summary.raw_record_latest_file_path.c_str()));

        std::ifstream capture_file("results/test_receive_runner_fake/capture_packets.bin", std::ios::binary);
        assert(capture_file.is_open());
        capture_file.seekg(0, std::ios::end);
        assert(capture_file.tellg() > 0);
    }

    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.capture_output_dir = "results/test_receive_runner_unavailable";
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
        context.config.capture_output_dir = "results/test_receive_runner_until_stopped";
        context.config.run_until_stopped = true;
        context.config.status_interval_seconds = 1;
        context.backend = std::make_unique<FakeBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();
        std::ostringstream status_stream;

        std::thread stopper([]()
                            {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            rxtech::request_receive_stop(); });

        rxtech::ReceiveRunner runner;
        runner.set_status_output(&status_stream);
        const rxtech::RunSummary summary = runner.run(context);
        stopper.join();

        assert(summary.run_status == "success");
        assert(summary.rx_packets > 0U);
        assert(summary.cpi_count == 1U);
        assert(summary.prt_count == 1U);
        assert(summary.channel_count == 1U);
        assert(summary.packet_count == summary.recorded_packets);
        assert(status_stream.str().find("实时接收状态") != std::string::npos);
        assert(status_stream.str().find("时间戳") != std::string::npos);
        assert(status_stream.str().find("链路判定") != std::string::npos);
        assert(status_stream.str().find("落盘记录") != std::string::npos);
        assert(status_stream.str().find("CPI 数量") != std::string::npos);
    }

    {
        rxtech::ReceiveContext context;
        context.config = rxtech::load_default_config();
        context.config.capture_output_dir = "results/test_receive_runner_filtered";
        context.config.duration_seconds = 1;
        context.config.receiver_ipv4 = "172.20.11.100";
        context.config.allowed_source_ipv4 = "172.20.11.222";
        context.config.allowed_dest_port = 9999U;
        context.backend = std::make_unique<FilteringFakeBackend>();
        context.metrics = std::make_unique<rxtech::MetricsCollector>();

        rxtech::ReceiveRunner runner;
        const rxtech::RunSummary summary = runner.run(context);

        assert(summary.run_status == "success");
        assert(summary.raw_rx_packets == 2U);
        assert(summary.filtered_packets == 1U);
        assert(summary.rx_packets == 1U);
        assert(summary.parsed_packets == 1U);
        assert(summary.data_packets == 1U);
        assert(summary.final_tail_packets == 1U);
        assert(summary.packet_count == 1U);
    }

    return 0;
}
