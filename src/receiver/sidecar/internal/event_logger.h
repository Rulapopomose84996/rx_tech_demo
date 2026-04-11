#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "event_schema.h"

namespace rxtech
{

    class IEventSink
    {
      public:
        virtual ~IEventSink() = default;
        virtual void write_line(const std::string &line) = 0;
        virtual void flush() = 0;
    };

    struct EventLoggerConfig
    {
        StructuredLogLevel min_level = StructuredLogLevel::info;
        std::vector<IEventSink *> sinks;
        std::uint32_t schema_version = kEventSchemaVersion;
    };

    class IEventLogger
    {
      public:
        virtual ~IEventLogger() = default;
        virtual void emit(const EventEnvelope &event) = 0;
        virtual void flush() = 0;
    };

    class EventLogger final : public IEventLogger
    {
      public:
        explicit EventLogger(EventLoggerConfig config);
        void emit(const EventEnvelope &event) override;
        void flush() override;

      private:
        EventLoggerConfig config_;
    };

} // namespace rxtech
