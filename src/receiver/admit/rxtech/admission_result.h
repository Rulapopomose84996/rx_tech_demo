#pragma once

#include <string>

namespace rxtech {

enum class AdmissionKind {
    start_new_cpi,
    write_active_cpi,
    switch_active_cpi,
    late_to_closed_cpi,
    stale_cpi,
    invalid_packet
};

struct AdmissionResult {
    AdmissionKind kind = AdmissionKind::invalid_packet;
    std::string reason;
};

const char* admission_kind_name(AdmissionKind kind) noexcept;

}  // namespace rxtech
