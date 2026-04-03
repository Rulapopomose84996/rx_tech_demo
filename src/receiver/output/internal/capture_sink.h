#pragma once

#include "rxtech/owner_loop.h"

#include "packet_pipeline.h"

namespace rxtech
{

    class CaptureSink
    {
    public:
        explicit CaptureSink(CaptureArtifacts &artifacts);

        void write(const ProcessedPacket &packet);
        void flush();

    private:
        CaptureArtifacts &artifacts_;
    };

} // namespace rxtech
