#include "rxtech/scenario.h"

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace rxtech {

namespace {

std::string trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string strip_quotes(const std::string& value) {
    if (value.size() >= 2U) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1U, value.size() - 2U);
        }
    }
    return value;
}

void assign_step_value(ScenarioStep& step, const std::string& key, const std::string& value) {
    const std::string normalized_key = trim(key);
    const std::string normalized_value = strip_quotes(trim(value));

    if (normalized_key == "name") {
        step.name = normalized_value;
    } else if (normalized_key == "phase") {
        step.phase = normalized_value;
    } else if (normalized_key == "traffic_profile") {
        step.traffic_profile = normalized_value;
    } else if (normalized_key == "packet_size_profile") {
        step.packet_size_profile = normalized_value;
    } else if (normalized_key == "target_rate_gbps") {
        step.target_rate_gbps = std::stod(normalized_value);
    } else if (normalized_key == "burst_multiplier") {
        step.burst_multiplier = std::stod(normalized_value);
    } else if (normalized_key == "duration_seconds") {
        step.duration_seconds = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "face_count") {
        step.face_count = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "packet_size_bytes") {
        step.packet_size_bytes = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "burst_window_ms") {
        step.burst_window_ms = static_cast<std::uint32_t>(std::stoul(normalized_value));
    }
}

void assign_scenario_value(Scenario& scenario, const std::string& key, const std::string& value) {
    const std::string normalized_key = trim(key);
    const std::string normalized_value = strip_quotes(trim(value));

    if (normalized_key == "scenario") {
        scenario.scenario_name = normalized_value;
    } else if (normalized_key == "packet_size_profile") {
        scenario.packet_size_profile = normalized_value;
        if (scenario.steps.empty()) {
            scenario.steps.push_back({});
        }
        scenario.steps.front().packet_size_profile = normalized_value;
    } else if (normalized_key == "packet_size_bytes") {
        scenario.default_packet_size_bytes = static_cast<std::uint32_t>(std::stoul(normalized_value));
        if (scenario.steps.empty()) {
            scenario.steps.push_back({});
        }
        scenario.steps.front().packet_size_bytes = scenario.default_packet_size_bytes;
    } else {
        if (scenario.steps.empty()) {
            scenario.steps.push_back({});
        }
        assign_step_value(scenario.steps.front(), normalized_key, normalized_value);
    }
}

void normalize_step_defaults(Scenario& scenario) {
    for (std::size_t index = 0; index < scenario.steps.size(); ++index) {
        ScenarioStep& step = scenario.steps[index];
        if (step.name.empty()) {
            step.name = index == 0U ? "measure" : ("step_" + std::to_string(index));
        }
        if (step.phase.empty()) {
            step.phase = step.name == "warmup" ? "warmup" : "measure";
        }
        if (step.traffic_profile.empty()) {
            step.traffic_profile = "steady";
        }
        if (step.packet_size_profile.empty()) {
            step.packet_size_profile = scenario.packet_size_profile.empty() ? "fixed" : scenario.packet_size_profile;
        }
        if (step.packet_size_bytes == 0U) {
            step.packet_size_bytes = scenario.default_packet_size_bytes != 0U ? scenario.default_packet_size_bytes : 512U;
        }
        if (step.duration_seconds == 0U) {
            step.duration_seconds = 5U;
        }
        if (step.face_count == 0U) {
            step.face_count = 1U;
        }
        if (step.burst_multiplier <= 0.0) {
            step.burst_multiplier = 1.0;
        }
    }
}

}  // namespace

bool is_measure_step(const ScenarioStep& step) {
    return step.phase != "warmup";
}

Scenario load_scenario(const std::string& path) {
    Scenario scenario;
    scenario.scenario_name = path.empty() ? "default_scenario" : path;
    scenario.packet_size_profile = "fixed";
    scenario.default_packet_size_bytes = 512U;

    if (path.empty() || path == "smoke") {
        scenario.scenario_name = path.empty() ? "default_scenario" : "smoke";
        scenario.steps.push_back({"warmup", "warmup", "steady", "fixed_128", 1.0, 1.0, 1U, 1U, 128U, 0U});
        scenario.steps.push_back({"measure", "measure", "steady", "fixed_512", 4.8, 1.0, 3U, 1U, 512U, 0U});
        return scenario;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open scenario: " + path);
    }

    bool in_steps = false;
    ScenarioStep* current_step = nullptr;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0U, comment_pos);
        }

        if (trim(line).empty()) {
            continue;
        }

        const bool indented = !line.empty() && std::isspace(static_cast<unsigned char>(line.front())) != 0;
        const std::string normalized = trim(line);
        if (normalized == "steps:") {
            in_steps = true;
            current_step = nullptr;
            continue;
        }

        if (in_steps && normalized.rfind("- ", 0) == 0) {
            scenario.steps.push_back({});
            current_step = &scenario.steps.back();
            const std::string payload = trim(normalized.substr(2U));
            if (!payload.empty()) {
                const std::size_t split = payload.find(':');
                if (split != std::string::npos) {
                    assign_step_value(*current_step, payload.substr(0U, split), payload.substr(split + 1U));
                }
            }
            continue;
        }

        const std::size_t split = normalized.find(':');
        if (split == std::string::npos) {
            continue;
        }

        if (in_steps && indented && current_step != nullptr) {
            assign_step_value(*current_step, normalized.substr(0U, split), normalized.substr(split + 1U));
        } else {
            assign_scenario_value(scenario, normalized.substr(0U, split), normalized.substr(split + 1U));
        }
    }

    if (scenario.steps.empty()) {
        scenario.steps.push_back({});
    }
    normalize_step_defaults(scenario);
    return scenario;
}

}  // namespace rxtech
