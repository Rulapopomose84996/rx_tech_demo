#pragma once

#include <chrono>
#include <iosfwd>
#include <string>
#include <vector>

#include "rxtech/receive_context.h"

namespace rxtech
{

std::vector<std::string> build_status_snapshot_lines_for_test(
    const RunSummary &summary,
    const std::chrono::steady_clock::duration &elapsed);

class StatusPanelWriter
{
public:
    explicit StatusPanelWriter(std::ostream *output);
    ~StatusPanelWriter();

    void render(const RunSummary &summary,
                const std::chrono::steady_clock::duration &elapsed);
    std::ostream *diagnostic_output() const;

private:
    void finish();

    std::ostream *output_ = nullptr;
    std::size_t line_count_ = 0;
    bool interactive_console_ = false;
    bool initialized_ = false;
};

} // namespace rxtech
