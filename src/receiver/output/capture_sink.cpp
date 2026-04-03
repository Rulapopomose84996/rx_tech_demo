#include "internal/capture_sink.h"

#include <ostream>
#include <stdexcept>

namespace rxtech
{
    namespace
    {

        void write_capture_index_header(std::ostream &index_stream)
        {
            index_stream << "cpi,channel,prt,packet_index,packet_kind,payload_len,valid\n";
        }

    } // namespace

    CaptureSink::CaptureSink(CaptureArtifacts &artifacts) : artifacts_(artifacts)
    {
        if (artifacts_.packet_stream == nullptr || artifacts_.index_stream == nullptr)
        {
            throw std::runtime_error("capture artifacts are incomplete");
        }
        write_capture_index_header(*artifacts_.index_stream);
    }

    void CaptureSink::write(const ProcessedPacket &packet)
    {
        artifacts_.packet_stream->write(reinterpret_cast<const char *>(packet.udp_frame.udp_payload.data()),
                                        static_cast<std::streamsize>(packet.udp_frame.udp_payload.size()));
        *artifacts_.index_stream << packet.interpreted.cpi
                                 << ',' << packet.interpreted.channel
                                 << ',' << packet.interpreted.prt
                                 << ',' << packet.interpreted.packet_index
                                 << ',' << packet_kind_name(packet.interpreted.kind)
                                 << ',' << packet.udp_frame.udp_payload.size()
                                 << ',' << (packet.interpreted.valid ? "true" : "false")
                                 << '\n';
        artifacts_.file_offset += packet.udp_frame.udp_payload.size();
        artifacts_.recorded_bytes += packet.udp_frame.udp_payload.size();
        ++artifacts_.recorded_packets;
        artifacts_.captured_bytes += packet.udp_frame.udp_payload.size();
        ++artifacts_.captured_packets;
    }

    void CaptureSink::flush()
    {
        artifacts_.packet_stream->flush();
        artifacts_.index_stream->flush();
    }

} // namespace rxtech
