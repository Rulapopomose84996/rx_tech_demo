#include "rxtech/owner_loop.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <unordered_set>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "rxtech/cpi_admission.h"
#include "rxtech/cpi_context_pool.h"
#include "rxtech/cpi_finalizer.h"
#include "rxtech/progress_tracker.h"
#include "rxtech/raw_frame_recorder.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/sample_packet_validator.h"
#include "rxtech/slot_writer.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/udp_payload_assembler.h"
#include "owner_loop_state.h"
#include "owner_loop_summary.h"
#include "status_panel.h"

namespace rxtech
{

    namespace
    {

        struct DataOrderCursor
        {
            std::uint16_t cpi = 0;
            std::uint16_t prt = 0;
            std::uint16_t channel = 0;
            std::uint16_t packet_index = 0;
        };

        DataOrderCursor build_next_expected_cursor(const InterpretedPacketView &packet,
                                                   const ProtocolSpec &spec)
        {
            DataOrderCursor next;
            next.cpi = packet.cpi;
            next.prt = packet.prt;
            next.channel = packet.channel;
            next.packet_index = packet.packet_index;

            if (next.packet_index < spec.packets_per_channel)
            {
                next.packet_index = static_cast<std::uint16_t>(next.packet_index + 1U);
                return next;
            }

            next.packet_index = 1U;
            if (next.channel + 1U < spec.channels_per_prt)
            {
                next.channel = static_cast<std::uint16_t>(next.channel + 1U);
                return next;
            }

            next.channel = 0U;
            next.prt = static_cast<std::uint16_t>(next.prt + 1U);
            return next;
        }

        bool matches_expected_cursor(const InterpretedPacketView &packet,
                                     const DataOrderCursor &expected)
        {
            return packet.cpi == expected.cpi &&
                   packet.prt == expected.prt &&
                   packet.channel == expected.channel &&
                   packet.packet_index == expected.packet_index;
        }

        std::string format_order_point(std::uint16_t cpi,
                                       std::uint16_t prt,
                                       std::uint16_t channel,
                                       std::uint16_t packet_index)
        {
            std::ostringstream out;
            out << "CPI " << cpi
                << " / PRT " << prt
                << " / CH " << channel
                << " / PKT " << packet_index;
            return out.str();
        }

        std::uint32_t parse_ipv4_be(const std::string &ipv4)
        {
            std::uint32_t octets[4] = {0U, 0U, 0U, 0U};
            char dot1 = '\0';
            char dot2 = '\0';
            char dot3 = '\0';
            std::istringstream stream(ipv4);
            if (!(stream >> octets[0] >> dot1 >> octets[1] >> dot2 >> octets[2] >> dot3 >> octets[3]))
            {
                return 0U;
            }
            if (dot1 != '.' || dot2 != '.' || dot3 != '.')
            {
                return 0U;
            }
            for (std::uint32_t octet : octets)
            {
                if (octet > 255U)
                {
                    return 0U;
                }
            }
            return (octets[0] << 24U) | (octets[1] << 16U) | (octets[2] << 8U) | octets[3];
        }

        bool matches_packet_filter(const RxConfig &config, const UdpPayloadFrame &frame)
        {
            const bool source_filter_enabled = !config.allowed_source_ipv4.empty();
            const bool dest_ip_filter_enabled = !config.receiver_ipv4.empty();
            const bool dest_port_filter_enabled = config.allowed_dest_port != 0U;
            if (!source_filter_enabled && !dest_ip_filter_enabled && !dest_port_filter_enabled)
            {
                return true;
            }
            if (source_filter_enabled && frame.source_ipv4_be != parse_ipv4_be(config.allowed_source_ipv4))
            {
                return false;
            }
            if (dest_ip_filter_enabled && frame.dest_ipv4_be != parse_ipv4_be(config.receiver_ipv4))
            {
                return false;
            }
            if (dest_port_filter_enabled && frame.dest_port != static_cast<std::uint16_t>(config.allowed_dest_port))
            {
                return false;
            }
            return true;
        }

    } // namespace

    namespace
    {
        void send_feedback_snapshot(const RxConfig &config, const RunSummary &summary, std::ostream *status_output)
        {
#ifdef __linux__
            if (!config.feedback_enabled || config.feedback_host.empty() || config.feedback_port == 0U)
            {
                return;
            }

            const int fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (fd < 0)
            {
                if (status_output != nullptr)
                {
                    *status_output << "[feedback] socket_error errno=" << errno << "\n";
                    status_output->flush();
                }
                return;
            }

            if (!config.feedback_bind_host.empty())
            {
                sockaddr_in bind_addr{};
                bind_addr.sin_family = AF_INET;
                bind_addr.sin_port = 0;
                if (inet_pton(AF_INET, config.feedback_bind_host.c_str(), &bind_addr.sin_addr) == 1)
                {
                    if (bind(fd, reinterpret_cast<const sockaddr *>(&bind_addr), sizeof(bind_addr)) != 0 && status_output != nullptr)
                    {
                        *status_output << "[feedback] bind_error errno=" << errno << " source=" << config.feedback_bind_host << "\n";
                        status_output->flush();
                    }
                }
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<std::uint16_t>(config.feedback_port));
            if (inet_pton(AF_INET, config.feedback_host.c_str(), &addr.sin_addr) != 1)
            {
                if (status_output != nullptr)
                {
                    *status_output << "[feedback] invalid_target=" << config.feedback_host << ":" << config.feedback_port << "\n";
                    status_output->flush();
                }
                close(fd);
                return;
            }

            std::ostringstream payload;
            payload << "{\"type\":\"receiver_feedback\""
                    << ",\"rx_packets\":" << summary.rx_packets
                    << ",\"rx_bytes\":" << summary.rx_bytes
                    << ",\"parsed_packets\":" << summary.parsed_packets
                    << ",\"control_table_packets\":" << summary.control_table_packets
                    << ",\"data_packets\":" << summary.data_packets
                    << ",\"loss_rate\":" << calculate_drop_rate(summary)
                    << ",\"queue_id\":" << summary.queue_id
                    << ",\"gbps\":" << summary.actual_rx_gbps
                    << "}";

            const std::string message = payload.str();
            const ssize_t sent = sendto(fd, message.data(), message.size(), 0, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
            if (sent < 0 && status_output != nullptr)
            {
                *status_output << "[feedback] send_error errno=" << errno << " target=" << config.feedback_host << ":" << config.feedback_port << "\n";
                status_output->flush();
            }
            close(fd);
#else
            (void)config;
            (void)summary;
            (void)status_output;
#endif
        }

        void write_capture_index_header(std::ostream &index_stream)
        {
            index_stream << "cpi,channel,prt,packet_index,packet_kind,payload_len,valid\n";
        }

        std::uint32_t read_u32_le_at(const std::uint8_t *data, std::size_t size, std::size_t offset)
        {
            if (data == nullptr || offset + 4U > size)
            {
                return 0U;
            }
            return static_cast<std::uint32_t>(data[offset + 0U]) |
                   (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
                   (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
                   (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
        }

        std::string hex_preview(const std::uint8_t *data, std::size_t size, std::size_t bytes_to_show)
        {
            std::ostringstream out;
            out << std::hex << std::setfill('0');
            const std::size_t count = std::min(size, bytes_to_show);
            for (std::size_t index = 0; index < count; ++index)
            {
                if (index != 0U)
                {
                    out << ' ';
                }
                out << std::setw(2) << static_cast<unsigned int>(data[index]);
            }
            return out.str();
        }

        void emit_invalid_packet_diagnostic(std::ostream &out,
                                            const PacketDesc &packet,
                                            const ParsedPacketView &parsed,
                                            RejectReason reason)
        {
            out << "[invalid-sample]"
                << " len=" << packet.len
                << " queue=" << packet.queue_id
                << " magic@0=0x" << std::hex << std::setw(8) << std::setfill('0') << read_u32_le_at(packet.data, packet.len, 0U)
                << std::dec
                << " reason=" << reject_reason_name(reason)
                << "\n";
            out << "[invalid-sample] decoded="
                << " kind=" << packet_kind_name(parsed.kind)
                << " cpi=" << parsed.cpi
                << " channel=" << parsed.channel
                << " prt=" << parsed.prt
                << " packet_index=" << parsed.packet_index
                << " tail=0x" << std::hex << std::setw(8) << std::setfill('0') << parsed.tail
                << std::dec
                << " payload_len=" << parsed.payload_len
                << " rx_tsc=" << parsed.rx_tsc;
            out << "\n";
            out << "[invalid-sample] preview=" << hex_preview(packet.data, packet.len, 64U) << "\n";
            out.flush();
        }

    } // namespace

    void OwnerLoop::set_status_output(std::ostream *output)
    {
        status_output_ = output;
    }

    RunSummary OwnerLoop::run(ReceiveContext &context,
                              CaptureArtifacts &artifacts,
                              const std::function<bool()> &should_stop) const
    {
        if (artifacts.packet_stream == nullptr || artifacts.index_stream == nullptr)
        {
            throw std::runtime_error("capture artifacts are incomplete");
        }

        const ProtocolSpec spec = protocol_spec_from_config(context.config);
        PacketParser parser{spec};
        PacketValidator validator{spec};
        ProtocolSequenceInterpreter sequence_interpreter{spec};
        UdpPayloadAssembler payload_assembler;
        write_capture_index_header(*artifacts.index_stream);

        const auto start_time = std::chrono::steady_clock::now();
        const auto status_interval = std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.status_interval_seconds));
        const auto feedback_interval = std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.feedback_interval_seconds));
        auto next_status_at = start_time + status_interval;
        auto next_feedback_at = start_time + feedback_interval;
        StatusPanelWriter status_panel(status_output_);
        std::string run_status = "success";
        std::string run_error;
        std::uint32_t invalid_dumped = 0;
        std::unordered_set<std::uint16_t> unique_cpis;
        std::unordered_set<std::uint16_t> unique_prts;
        std::unordered_set<std::uint16_t> unique_channels;
        std::uint64_t filtered_packets = 0;
        std::map<std::uint16_t, ProtocolChannelStats> channel_stats;
        std::map<std::uint64_t, ProtocolCpiStats> cpi_stats;
        std::map<std::pair<std::uint64_t, std::uint16_t>, ProtocolPrtCoverage> prt_coverage;
        std::uint64_t final_tail_packets = 0;
        std::uint16_t latest_data_cpi = 0;
        std::uint16_t latest_data_prt = 0;
        bool latest_data_seen = false;
        std::uint64_t data_order_checked_packets = 0;
        bool data_order_initialized = false;
        bool data_order_matches_expected = true;
        bool data_order_channel_batched = false;
        std::string data_order_first_mismatch;
        DataOrderCursor expected_next_packet{};
        InterpretedPacketView previous_data_packet{};
        CpiContextPool ctx_pool;
        RecentClosedRing closed_ring;
        CpiAdmission admission;
        SlotWriter slot_writer;
        ProgressTracker progress_tracker;
        CpiFinalizer finalizer;
        std::uint32_t active_ctx_index = kInvalidPoolIndex;
        CpiContext *active_ctx = nullptr;

        const auto populate_active_prt_summary = [&](RunSummary &target)
        {
            if (!latest_data_seen)
            {
                return;
            }

            target.active_prt_available = true;
            target.active_cpi = latest_data_cpi;
            target.active_prt_packets_per_channel = spec.packets_per_channel;
            target.active_prt_channels.clear();
            target.active_prt_channel_count = 0;
            target.active_prt_complete = false;

            auto selected_it = prt_coverage.end();
            auto fallback_it = prt_coverage.end();
            for (auto it = prt_coverage.begin(); it != prt_coverage.end(); ++it)
            {
                if (it->first.first != static_cast<std::uint64_t>(latest_data_cpi))
                {
                    continue;
                }

                fallback_it = it;

                bool complete = true;
                for (std::uint16_t channel = 0; channel < spec.channels_per_prt; ++channel)
                {
                    const auto channel_it = it->second.find(channel);
                    if (channel_it == it->second.end() ||
                        channel_it->second.size() != static_cast<std::size_t>(spec.packets_per_channel))
                    {
                        complete = false;
                        break;
                    }
                }

                if (!complete)
                {
                    selected_it = it;
                    break;
                }
            }

            if (selected_it == prt_coverage.end())
            {
                selected_it = fallback_it;
            }

            if (selected_it == prt_coverage.end())
            {
                target.active_prt = latest_data_prt;
                return;
            }

            target.active_prt = selected_it->first.second;

            std::uint64_t observed_channels = 0;
            bool complete = true;
            target.active_prt_channels.reserve(spec.channels_per_prt);
            for (std::uint16_t channel = 0; channel < spec.channels_per_prt; ++channel)
            {
                ProtocolPrtChannelCoverageSummary channel_summary;
                channel_summary.channel = channel;

                const auto channel_it = selected_it->second.find(channel);
                if (channel_it != selected_it->second.end())
                {
                    channel_summary.packet_count = static_cast<std::uint64_t>(channel_it->second.size());
                    if (channel_summary.packet_count > 0U)
                    {
                        ++observed_channels;
                    }
                }

                channel_summary.complete = channel_summary.packet_count == spec.packets_per_channel;
                if (!channel_summary.complete)
                {
                    complete = false;
                }

                target.active_prt_channels.push_back(channel_summary);
            }

            target.active_prt_channel_count = observed_channels;
            target.active_prt_complete = complete && !target.active_prt_channels.empty();
        };

        const auto populate_data_order_summary = [&](RunSummary &target)
        {
            target.data_order_checked_packets = data_order_checked_packets;
            if (data_order_checked_packets == 0U)
            {
                target.data_order_assessment = "无数据包";
                return;
            }

            if (data_order_matches_expected)
            {
                target.data_order_assessment = "符合按 PRT 推进的和/差/差顺序";
                return;
            }

            target.data_order_assessment = data_order_channel_batched
                                               ? "偏离按 PRT 推进顺序，当前捕获更像按通道分批到达"
                                               : "偏离按 PRT 推进顺序";
            target.data_order_first_mismatch = data_order_first_mismatch;
        };

        const auto release_active_ctx = [&]()
        {
            if (active_ctx_index != kInvalidPoolIndex)
            {
                ctx_pool.release(active_ctx_index);
                active_ctx_index = kInvalidPoolIndex;
                active_ctx = nullptr;
            }
        };

        const auto open_active_ctx = [&](std::uint16_t cpi_id)
        {
            release_active_ctx();
            active_ctx_index = ctx_pool.acquire(cpi_id);
            active_ctx = ctx_pool.get(active_ctx_index);
            if (active_ctx == nullptr)
            {
                active_ctx_index = kInvalidPoolIndex;
                run_status = "error";
                run_error = "cpi context pool exhausted";
                context.metrics->on_error();
                return false;
            }
            return true;
        };

        const auto finalize_active_ctx = [&](std::uint32_t trigger)
        {
            if (active_ctx == nullptr)
            {
                return;
            }
            const std::optional<CpiOutput> output = finalizer.try_finalize(*active_ctx, trigger);
            if (output.has_value())
            {
                closed_ring.push(output->cpi_id, output->seal_tsc, output->decision);
            }
            release_active_ctx();
        };

        while (!should_stop())
        {
            RxBurst burst;
            if (!context.backend->recv_burst(burst, context.config.max_burst))
            {
                run_status = "error";
                run_error = "backend recv_burst failed";
                context.backend->release_burst(burst);
                break;
            }

            std::uint64_t burst_bytes = 0;
            std::size_t accepted_packets = 0U;
            for (const PacketDesc &packet : burst.packets)
            {
                if (artifacts.raw_frame_recorder != nullptr)
                {
                    artifacts.raw_frame_recorder->submit(packet);
                }

                const auto udp_frames = payload_assembler.push(packet);
                for (const auto &udp_frame : udp_frames)
                {
                    if (!matches_packet_filter(context.config, udp_frame))
                    {
                        ++filtered_packets;
                        continue;
                    }

                    burst_bytes += udp_frame.udp_payload.size();
                    ++accepted_packets;
                    const ParsedPacketView parsed = parser.parse(udp_frame);
                    const PacketValidity validation = validator.validate(parsed);
                    if (!validation.ok)
                    {
                        context.metrics->on_reject(validation.reason);
                        if (invalid_dumped < 5U)
                        {
                            PacketDesc diagnostic_packet;
                            diagnostic_packet.data = const_cast<std::uint8_t *>(udp_frame.udp_payload.data());
                            diagnostic_packet.len = static_cast<std::uint32_t>(udp_frame.udp_payload.size());
                            diagnostic_packet.queue_id = packet.queue_id;
                            std::ostream &diagnostic_stream = *status_panel.diagnostic_output();
                            emit_invalid_packet_diagnostic(diagnostic_stream, diagnostic_packet, parsed, validation.reason);
                            ++invalid_dumped;
                        }
                        continue;
                    }

                    const InterpretedPacketView protocol_packet = sequence_interpreter.interpret(parsed);
                    if (!protocol_packet.valid)
                    {
                        context.metrics->on_reject(protocol_packet.reject_reason);
                        if (invalid_dumped < 5U)
                        {
                            PacketDesc diagnostic_packet;
                            diagnostic_packet.data = const_cast<std::uint8_t *>(udp_frame.udp_payload.data());
                            diagnostic_packet.len = static_cast<std::uint32_t>(udp_frame.udp_payload.size());
                            diagnostic_packet.queue_id = packet.queue_id;
                            std::ostream &diagnostic_stream = *status_panel.diagnostic_output();
                            emit_invalid_packet_diagnostic(
                                diagnostic_stream, diagnostic_packet, parsed, protocol_packet.reject_reason);
                            ++invalid_dumped;
                        }
                        continue;
                    }

                    context.metrics->on_valid_packet(protocol_packet.kind);
                    if (packet.ts_ns != 0U)
                    {
                        context.metrics->on_packet_latency_ns(packet.ts_ns);
                    }
                    unique_cpis.insert(protocol_packet.cpi);
                    if (protocol_packet.kind == PacketKind::data_packet)
                    {
                        ++data_order_checked_packets;
                        if (!data_order_initialized)
                        {
                            expected_next_packet = build_next_expected_cursor(protocol_packet, spec);
                            data_order_initialized = true;
                        }
                        else if (data_order_matches_expected && !matches_expected_cursor(protocol_packet, expected_next_packet))
                        {
                            data_order_matches_expected = false;
                            if (previous_data_packet.packet_index == spec.packets_per_channel &&
                                protocol_packet.packet_index == 1U &&
                                protocol_packet.channel == previous_data_packet.channel &&
                                protocol_packet.prt == static_cast<std::uint16_t>(previous_data_packet.prt + 1U))
                            {
                                data_order_channel_batched = true;
                            }

                            std::ostringstream mismatch;
                            mismatch << "第 " << data_order_checked_packets << " 个数据包开始偏离，期望 "
                                     << format_order_point(expected_next_packet.cpi,
                                                           expected_next_packet.prt,
                                                           expected_next_packet.channel,
                                                           expected_next_packet.packet_index)
                                     << "，实际 "
                                     << format_order_point(protocol_packet.cpi,
                                                           protocol_packet.prt,
                                                           protocol_packet.channel,
                                                           protocol_packet.packet_index);
                            data_order_first_mismatch = mismatch.str();
                        }
                        expected_next_packet = build_next_expected_cursor(protocol_packet, spec);
                        previous_data_packet = protocol_packet;

                        if (active_ctx == nullptr && !open_active_ctx(protocol_packet.cpi))
                        {
                            continue;
                        }

                        AdmissionResult admission_result =
                            admission.judge(parsed, active_ctx->header.cpi_id, closed_ring);
                        if (admission_result == AdmissionResult::TRIGGER_CPI_SWITCH)
                        {
                            finalize_active_ctx(TriggerCpiSwitch);
                            if (!open_active_ctx(protocol_packet.cpi))
                            {
                                continue;
                            }
                            admission_result = AdmissionResult::WRITE_ACTIVE;
                        }
                        if (admission_result != AdmissionResult::WRITE_ACTIVE)
                        {
                            context.metrics->on_drop();
                            continue;
                        }

                        const SlotWriteResult slot_write = slot_writer.write(*active_ctx, parsed);
                        if (slot_write.duplicate)
                        {
                            context.metrics->on_drop();
                            continue;
                        }
                        if (!slot_write.first_write)
                        {
                            context.metrics->on_reject(slot_write.reason);
                            continue;
                        }

                        progress_tracker.advance(
                            *active_ctx, protocol_packet.prt, protocol_packet.channel, parsed.tail == spec.magic_tail);
                        if ((active_ctx->header.trigger_bits & TriggerFullReady) != 0U)
                        {
                            finalize_active_ctx(TriggerFullReady);
                        }

                        ProtocolChannelStats &per_channel = channel_stats[protocol_packet.channel];
                        per_channel.data_packets += 1U;
                        per_channel.iq_count += protocol_packet.iq_count;
                        ProtocolCpiStats &stats = cpi_stats[protocol_packet.cpi];
                        stats.data_packets += 1U;
                        stats.prts.insert(protocol_packet.prt);
                        prt_coverage[{protocol_packet.cpi, protocol_packet.prt}][protocol_packet.channel].insert(protocol_packet.packet_index);
                        latest_data_cpi = protocol_packet.cpi;
                        latest_data_prt = protocol_packet.prt;
                        latest_data_seen = true;
                        if (parsed.tail == spec.magic_tail)
                        {
                            ++final_tail_packets;
                        }
                    }

                    CapturedPacket captured;
                    captured.payload = udp_frame.udp_payload;
                    captured.cpi = protocol_packet.cpi;
                    captured.channel = protocol_packet.channel;
                    captured.prt = protocol_packet.prt;
                    captured.packet_index = protocol_packet.packet_index;
                    captured.packet_kind = packet_kind_name(protocol_packet.kind);
                    captured.valid = protocol_packet.valid;
                    if (protocol_packet.kind == PacketKind::data_packet)
                    {
                        unique_prts.insert(protocol_packet.prt);
                        unique_channels.insert(protocol_packet.channel);
                    }

                    artifacts.packet_stream->write(reinterpret_cast<const char *>(captured.payload.data()),
                                                   static_cast<std::streamsize>(captured.payload.size()));
                    *artifacts.index_stream << captured.cpi
                                            << ',' << captured.channel
                                            << ',' << captured.prt
                                            << ',' << captured.packet_index
                                            << ',' << captured.packet_kind
                                            << ',' << captured.payload.size()
                                            << ',' << (captured.valid ? "true" : "false")
                                            << '\n';

                    artifacts.file_offset += captured.payload.size();
                    artifacts.recorded_bytes += captured.payload.size();
                    ++artifacts.recorded_packets;
                    artifacts.captured_bytes += captured.payload.size();
                    ++artifacts.captured_packets;
                }
            }

            if (accepted_packets > 0U)
            {
                context.metrics->on_burst(accepted_packets, burst_bytes);
            }
            context.backend->release_burst(burst);

            const auto now = std::chrono::steady_clock::now();
            if (context.config.run_until_stopped && ((status_output_ != nullptr && now >= next_status_at) ||
                                                     (context.config.feedback_enabled && now >= next_feedback_at)))
            {
                RunSummary status_summary =
                    context.metrics->finalize(context.backend->name(),
                                              "light_parse",
                                              "sample_replay",
                                              std::max<std::uint32_t>(
                                                  1U,
                                                  static_cast<std::uint32_t>(
                                                      std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count())));
                status_summary.captured_packets = artifacts.captured_packets;
                status_summary.captured_bytes = artifacts.captured_bytes;
                status_summary.recorded_packets = artifacts.recorded_packets;
                status_summary.recorded_bytes = artifacts.recorded_bytes;
                status_summary.filtered_packets = filtered_packets;
                status_summary.packet_count = artifacts.recorded_packets;
                status_summary.cpi_count = static_cast<std::uint64_t>(unique_cpis.size());
                status_summary.prt_count = static_cast<std::uint64_t>(unique_prts.size());
                status_summary.channel_count = static_cast<std::uint64_t>(unique_channels.size());
                populate_data_order_summary(status_summary);
                populate_active_prt_summary(status_summary);
                merge_backend_stats(status_summary, context.backend->stats());
                apply_raw_record_stats(status_summary, artifacts.raw_frame_recorder);
                if (status_output_ != nullptr && now >= next_status_at)
                {
                    status_panel.render(status_summary, now - start_time);
                    next_status_at = now + status_interval;
                }
                if (context.config.feedback_enabled && now >= next_feedback_at)
                {
                    send_feedback_snapshot(context.config, status_summary, status_panel.diagnostic_output());
                    next_feedback_at = now + feedback_interval;
                }
            }
        }

        artifacts.packet_stream->flush();
        artifacts.index_stream->flush();

        const auto end_time = std::chrono::steady_clock::now();
        const auto elapsed_seconds = std::max<std::uint32_t>(
            1U,
            static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()));

        RunSummary summary = context.metrics->finalize(context.backend->name(), "light_parse", "sample_replay", elapsed_seconds);
        summary.run_status = run_status;
        summary.error_message = run_error;
        summary.backend_available = true;
        summary.backend_status = "available";
        summary.captured_packets = artifacts.captured_packets;
        summary.captured_bytes = artifacts.captured_bytes;
        summary.recorded_packets = artifacts.recorded_packets;
        summary.recorded_bytes = artifacts.recorded_bytes;
        summary.filtered_packets = filtered_packets;
        summary.packet_count = artifacts.recorded_packets;
        summary.cpi_count = static_cast<std::uint64_t>(unique_cpis.size());
        summary.prt_count = static_cast<std::uint64_t>(unique_prts.size());
        summary.channel_count = static_cast<std::uint64_t>(unique_channels.size());
        populate_data_order_summary(summary);
        populate_active_prt_summary(summary);
        summary.final_tail_packets = final_tail_packets;
        for (const auto &entry : prt_coverage)
        {
            bool complete = true;
            for (std::uint16_t channel = 0; channel < spec.channels_per_prt; ++channel)
            {
                const auto channel_it = entry.second.find(channel);
                if (channel_it == entry.second.end())
                {
                    complete = false;
                    break;
                }
                for (std::uint16_t packet_index = 1U; packet_index <= spec.packets_per_channel; ++packet_index)
                {
                    if (channel_it->second.count(packet_index) == 0U)
                    {
                        complete = false;
                        break;
                    }
                }
                if (!complete)
                {
                    break;
                }
            }
            if (complete)
            {
                ++summary.complete_prt_count;
            }
        }
        for (const auto &entry : channel_stats)
        {
            ProtocolChannelSummary channel_summary;
            channel_summary.channel = entry.first;
            channel_summary.data_packets = entry.second.data_packets;
            channel_summary.iq_count = entry.second.iq_count;
            summary.protocol_channels.push_back(channel_summary);
        }
        for (const auto &entry : cpi_stats)
        {
            ProtocolCpiSummary cpi_summary;
            cpi_summary.cpi = entry.first;
            cpi_summary.data_packets = entry.second.data_packets;
            cpi_summary.prt_count = static_cast<std::uint64_t>(entry.second.prts.size());
            summary.protocol_cpis.push_back(cpi_summary);
        }
        merge_backend_stats(summary, context.backend->stats());
        apply_raw_record_stats(summary, artifacts.raw_frame_recorder);
        summary.human_summary = build_run_human_summary(summary);

        if (context.config.run_until_stopped && status_output_ != nullptr)
        {
            status_panel.render(summary, end_time - start_time);
        }
        send_feedback_snapshot(context.config, summary, status_panel.diagnostic_output());
        release_active_ctx();

        return summary;
    }

} // namespace rxtech
