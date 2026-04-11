#include "rxtech/file_replay_backend.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

// replay_sender helpers are compiled into this translation unit via the
// rx_receiver_filereplay CMake target, which adds src/replay_sender to the
// include path.
#include "manifest_loader.h"
#include "frame_builder.h"

namespace rxtech
{

    namespace
    {
        constexpr std::size_t kReplayEthHeaderBytes = 14U;
        constexpr std::size_t kReplayIpv4HeaderBytes = 20U;
        constexpr std::size_t kReplayUdpHeaderBytes = 8U;
        constexpr std::size_t kReplayUdpPayloadOffset =
            kReplayEthHeaderBytes + kReplayIpv4HeaderBytes + kReplayUdpHeaderBytes;
    } // namespace

    // ── Implementation details ───────────────────────────────────────────────

    struct FileReplayBackend::Impl
    {
        FileReplayOptions opts;
        std::uint32_t src_ipv4_be = 0;
        std::uint32_t dst_ipv4_be = 0;
        std::uint16_t src_port = 0;
        std::uint16_t dst_port = 0;

        // Pre-built Ethernet frames, one per replay entry.
        // Each inner vector owns its bytes for pointer stability.
        std::vector<std::vector<std::uint8_t>> frames;

        // Replay state
        std::size_t cursor = 0; ///< Next frame index to serve
        std::uint32_t loop = 0; ///< Completed loop count

        // Rate limiting
        std::uint64_t ns_per_packet = 0; ///< 0 = no rate limit
        std::uint64_t next_send_ns = 0;

        // Stats
        BackendStats stats{};
        bool stopped = false;
    };

    // ── Construction / destruction ───────────────────────────────────────────

    FileReplayBackend::FileReplayBackend(FileReplayOptions opts) : impl_(std::make_unique<Impl>())
    {
        impl_->opts = std::move(opts);
    }

    FileReplayBackend::~FileReplayBackend() = default;

    // ── IRxBackend::init ─────────────────────────────────────────────────────

    BackendInitResult FileReplayBackend::init(const RxConfig &config)
    {
        BackendInitResult result;

        // Resolve addressing defaults from config when not overridden.
        replay::FrameConfig fcfg;
        if (impl_->opts.dst_ipv4_be != 0)
            fcfg.dst_ipv4_be = impl_->opts.dst_ipv4_be;
        else
        {
            // Parse receiver_ipv4 from config (e.g. "172.20.11.100")
            const auto &ip = config.ingress.receiver_ipv4;
            if (!ip.empty())
            {
                unsigned a{}, b{}, c{}, d{};
                if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
                    fcfg.dst_ipv4_be = (static_cast<std::uint32_t>(a) << 24U) | (static_cast<std::uint32_t>(b) << 16U) |
                                       (static_cast<std::uint32_t>(c) << 8U) | static_cast<std::uint32_t>(d);
            }
        }
        if (impl_->opts.src_ipv4_be != 0)
            fcfg.src_ipv4_be = impl_->opts.src_ipv4_be;
        else
        {
            const auto &src_ip = config.ingress.allowed_source_ipv4;
            if (!src_ip.empty())
            {
                unsigned a{}, b{}, c{}, d{};
                if (std::sscanf(src_ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
                    fcfg.src_ipv4_be = (static_cast<std::uint32_t>(a) << 24U) | (static_cast<std::uint32_t>(b) << 16U) |
                                       (static_cast<std::uint32_t>(c) << 8U) | static_cast<std::uint32_t>(d);
            }
        }
        if (impl_->opts.dst_port != 0)
            fcfg.dst_port = impl_->opts.dst_port;
        else
            fcfg.dst_port = static_cast<std::uint16_t>(config.ingress.allowed_dest_port);

        if (impl_->opts.src_port != 0)
            fcfg.src_port = impl_->opts.src_port;

        try
        {
            // Load all replay entries
            const auto entries = replay::load_replay_entries(impl_->opts.data_dirs);
            if (entries.empty())
            {
                result.reason = "未找到可回放的数据条目";
                return result;
            }

            impl_->frames.reserve(entries.size());

            std::uint16_t seq = 0;
            for (const auto &entry : entries)
            {
                // Read payload from bin file
                std::ifstream f(entry.bin_file, std::ios::binary);
                if (!f.is_open())
                    throw std::runtime_error("无法打开二进制文件: " + entry.bin_file);
                f.seekg(static_cast<std::streamoff>(entry.offset));
                std::vector<std::uint8_t> payload(entry.length);
                f.read(reinterpret_cast<char *>(payload.data()), entry.length);
                if (!f)
                    throw std::runtime_error("读取数据长度不足: " + entry.bin_file);

                // Build Ethernet frame
                auto frame = replay::build_eth_frame(payload.data(), entry.length, fcfg, seq++);
                impl_->frames.push_back(std::move(frame));
            }
        }
        catch (const std::exception &ex)
        {
            result.reason = ex.what();
            return result;
        }

        impl_->src_ipv4_be = fcfg.src_ipv4_be;
        impl_->dst_ipv4_be = fcfg.dst_ipv4_be;
        impl_->src_port = fcfg.src_port;
        impl_->dst_port = fcfg.dst_port;

        // Rate limiting setup
        if (impl_->opts.pps > 0)
            impl_->ns_per_packet = 1'000'000'000ULL / impl_->opts.pps;

        impl_->next_send_ns = steady_clock_now_ns();
        result.ok = true;
        return result;
    }

    // ── IRxBackend::recv_burst ───────────────────────────────────────────────

    bool FileReplayBackend::recv_burst(UdpDatagramBurst &burst, std::uint32_t max_burst)
    {
        burst.datagrams.clear();
        if (impl_->stopped || impl_->frames.empty())
            return true;

        const std::uint32_t budget =
            std::min<std::uint32_t>(max_burst, static_cast<std::uint32_t>(burst.datagrams.max_size()));

        // Check loop completion
        if (impl_->cursor >= impl_->frames.size())
        {
            ++impl_->loop;
            if (impl_->loop >= impl_->opts.loop_count)
            {
                impl_->stopped = true;
                return true; // signal "no more data but not an error"
            }
            impl_->cursor = 0;
        }

        // Rate limiting: sleep until next slot
        if (impl_->ns_per_packet > 0 && budget > 0U)
        {
            const std::uint64_t now = steady_clock_now_ns();
            if (now < impl_->next_send_ns)
            {
                const std::uint64_t wait_ns = impl_->next_send_ns - now;
                if (wait_ns > 1'000'000ULL) // only sleep for >1ms to avoid busy-spin
                    std::this_thread::sleep_for(std::chrono::nanoseconds(wait_ns));
            }
        }

        // Fill burst
        const std::uint64_t ts = steady_clock_now_ns();
        std::uint32_t served = 0;
        while (served < budget && impl_->cursor < impl_->frames.size())
        {
            const std::size_t frame_index = impl_->cursor;
            const auto &frame = impl_->frames[impl_->cursor];
            if (frame.size() < kReplayUdpPayloadOffset)
            {
                ++impl_->cursor;
                ++impl_->stats.rx_errors;
                continue;
            }

            UdpDatagramDesc datagram;
            datagram.raw_frame_data = frame.data();
            datagram.raw_frame_len = static_cast<std::uint32_t>(frame.size());
            datagram.payload_data = frame.data() + kReplayUdpPayloadOffset;
            datagram.payload_len = static_cast<std::uint32_t>(frame.size() - kReplayUdpPayloadOffset);
            datagram.src_ipv4_be = impl_->src_ipv4_be;
            datagram.dst_ipv4_be = impl_->dst_ipv4_be;
            datagram.src_port = impl_->src_port;
            datagram.dst_port = impl_->dst_port;
            datagram.ts_ns = ts;
            datagram.cookie = static_cast<std::uintptr_t>(frame_index);
            datagram.backend_kind = BackendKind::file_replay;
            burst.datagrams.push_back(datagram);
            ++impl_->cursor;
            ++served;
            ++impl_->stats.rx_packets;
            impl_->stats.rx_bytes += datagram.raw_frame_len;
        }

        ++impl_->stats.rx_polls;
        if (served == 0)
            ++impl_->stats.empty_polls;

        // Advance rate limiter
        if (impl_->ns_per_packet > 0 && served > 0)
            impl_->next_send_ns += impl_->ns_per_packet * served;

        return true;
    }

    // ── IRxBackend::release_burst / stats / shutdown ─────────────────────────

    void FileReplayBackend::release_burst(UdpDatagramBurst &burst)
    {
        burst.datagrams.clear();
    }

    BackendStats FileReplayBackend::stats() const
    {
        return impl_->stats;
    }

    void FileReplayBackend::shutdown()
    {
        impl_->stopped = true;
    }

} // namespace rxtech
