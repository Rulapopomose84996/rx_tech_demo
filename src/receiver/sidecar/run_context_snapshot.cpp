#include "internal/run_context_snapshot.h"

#include "internal/structured_logger.h"
#include "internal/summary_renderer.h"

namespace rxtech
{

    namespace
    {

        const char *build_mode_name() noexcept
        {
#ifdef NDEBUG
            return "release";
#else
            return "debug";
#endif
        }

    } // namespace

    RunHeaderSnapshot build_run_header_snapshot(const RxConfig &config)
    {
        RunHeaderSnapshot snapshot;
        snapshot.backend = config.process.backend_name;
        snapshot.build_mode = build_mode_name();
        snapshot.config_path = config.process.config_path;
        snapshot.run_id = config.process.run_label;
        snapshot.run_dir = config.operations.output_dir;
        snapshot.events_path = structured_logger_events_path();
        if (snapshot.events_path.empty())
        {
            snapshot.events_path = default_events_log_path(config);
        }
        snapshot.summary_json_path = summary_json_path(snapshot.run_dir);
        snapshot.summary_text_path = summary_text_path(snapshot.run_dir);
        return snapshot;
    }

    nlohmann::json render_run_header_event_payload(const RunHeaderSnapshot &snapshot)
    {
        return {{"backend", snapshot.backend},
                {"build_mode", snapshot.build_mode},
                {"config_path", snapshot.config_path},
                {"run_id", snapshot.run_id},
                {"host", snapshot.host},
                {"logging", {{"events_path", snapshot.events_path}}},
                {"artifacts",
                 {{"run_dir", snapshot.run_dir},
                  {"summary_json_path", snapshot.summary_json_path},
                  {"summary_text_path", snapshot.summary_text_path}}}};
    }

} // namespace rxtech
