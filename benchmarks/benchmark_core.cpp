#include <array>
#include <cstdint>

#include <benchmark/benchmark.h>

#include "rxtech/cpi_context.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/slot_writer.h"
#include "rxtech/spsc_ring.h"

namespace
{

    std::array<std::uint8_t, 14U + 20U + 8U + 2048U> make_packet_frame()
    {
        std::array<std::uint8_t, 14U + 20U + 8U + 2048U> frame{};
        frame[12] = 0x08U;
        frame[13] = 0x00U;
        frame[14] = 0x45U;
        frame[16] = 0x08U;
        frame[17] = 0x1cU;
        frame[23] = 17U;
        frame[34] = 0xe4U;
        frame[35] = 0x79U;
        frame[36] = 0x27U;
        frame[37] = 0x0fU;
        frame[38] = 0x08U;
        frame[39] = 0x08U;
        frame[42] = 0x03U;
        frame[43] = 0xffU;
        frame[44] = 0xaaU;
        frame[45] = 0x55U;
        frame[46] = 0x01U;
        frame[50] = 0x01U;
        return frame;
    }

} // namespace

static void BM_PacketParserParse(benchmark::State &state)
{
    const auto frame = make_packet_frame();
    rxtech::PacketDesc packet;
    packet.data = const_cast<std::uint8_t *>(frame.data());
    packet.len = static_cast<std::uint32_t>(frame.size());
    rxtech::PacketParser parser;

    for (auto _ : state)
    {
        auto parsed = parser.parse(packet);
        benchmark::DoNotOptimize(parsed.valid);
        benchmark::DoNotOptimize(parsed.cpi);
        benchmark::DoNotOptimize(parsed.packet_index);
    }
}
BENCHMARK(BM_PacketParserParse);

static void BM_SpscRingRoundTrip(benchmark::State &state)
{
    rxtech::SpscRing<std::uint32_t> ring(1024U);
    for (auto _ : state)
    {
        for (std::uint32_t index = 0; index < 256U; ++index)
        {
            benchmark::DoNotOptimize(ring.push(index));
        }
        std::uint32_t value = 0U;
        while (ring.pop(value))
        {
            benchmark::DoNotOptimize(value);
        }
    }
}
BENCHMARK(BM_SpscRingRoundTrip);

static void BM_CpiContextReset(benchmark::State &state)
{
    rxtech::CpiContext context;
    for (auto _ : state)
    {
        context.reset(7U, 1U);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_CpiContextReset);

static void BM_SlotWriterFirstWrite(benchmark::State &state)
{
    rxtech::ProtocolSpec spec;
    rxtech::SlotWriter writer(spec);
    std::array<std::uint8_t, rxtech::kCpiSlotStride> payload{};

    for (auto _ : state)
    {
        rxtech::CpiContext context;
        context.reset(1U, 0U);
        rxtech::ControlSnapshot snapshot;
        snapshot.cpi_id = 1U;
        snapshot.n_prt = 1U;
        snapshot.channel_count = 3U;
        snapshot.packets_per_channel = 9U;
        snapshot.valid = true;
        rxtech::bind_control_snapshot(context, snapshot);

        rxtech::ParsedPacketView packet;
        packet.valid = true;
        packet.kind = rxtech::PacketKind::data_packet;
        packet.cpi = 1U;
        packet.prt = 1U;
        packet.channel = 0U;
        packet.packet_index = 1U;
        packet.payload_ptr = payload.data();
        packet.payload_len = static_cast<std::uint32_t>(payload.size());

        auto result = writer.write(context, packet);
        benchmark::DoNotOptimize(result.first_write);
        benchmark::DoNotOptimize(result.duplicate);
        benchmark::DoNotOptimize(result.slot_index);
    }
}
BENCHMARK(BM_SlotWriterFirstWrite);
