#include "rxtech/rx_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

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

bool parse_bool(const std::string& value) {
    const std::string normalized = trim(value);
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

std::vector<int> parse_int_list(const std::string& value) {
    std::string normalized = trim(value);
    if (!normalized.empty() && normalized.front() == '[' && normalized.back() == ']') {
        normalized = normalized.substr(1U, normalized.size() - 2U);
    }

    std::vector<int> result;
    std::stringstream stream(normalized);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            result.push_back(std::stoi(item));
        }
    }
    return result;
}

void assign_config_value(RxConfig& config, const std::string& key, const std::string& value) {
    const std::string normalized_key = trim(key);
    const std::string normalized_value = strip_quotes(trim(value));

    if (normalized_key == "backend" || normalized_key == "backend_name") {
        config.backend_name = normalized_value;
    } else if (normalized_key == "mode" || normalized_key == "mode_name") {
        config.mode_name = normalized_value;
    } else if (normalized_key == "scenario" || normalized_key == "scenario_path") {
        config.scenario_path = normalized_value;
    } else if (normalized_key == "output" || normalized_key == "output_dir") {
        config.output_dir = normalized_value;
    } else if (normalized_key == "interface" || normalized_key == "interface_name") {
        config.interface_name = normalized_value;
    } else if (normalized_key == "bind_address") {
        config.bind_address = normalized_value;
    } else if (normalized_key == "queue_id") {
        config.queue_id = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "max_burst") {
        config.max_burst = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "duration_seconds") {
        config.duration_seconds = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "udp_port") {
        config.udp_port = static_cast<std::uint16_t>(std::stoul(normalized_value));
    } else if (normalized_key == "packet_size_bytes") {
        config.packet_size_bytes = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "socket_poll_timeout_ms") {
        config.socket_poll_timeout_ms = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "cpu_cores") {
        config.cpu_cores = parse_int_list(normalized_value);
    } else if (normalized_key == "enable_internal_traffic") {
        config.enable_internal_traffic = parse_bool(normalized_value);
    } else if (normalized_key == "xdp_bind_mode") {
        config.xdp_bind_mode = normalized_value;
    } else if (normalized_key == "dpdk_port_id") {
        config.dpdk_port_id = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "dpdk_pci_addr") {
        config.dpdk_pci_addr = normalized_value;
    } else if (normalized_key == "dpdk_socket_mem_mb") {
        config.dpdk_socket_mem_mb = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "dpdk_mempool_size") {
        config.dpdk_mempool_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "dpdk_mbuf_cache_size") {
        config.dpdk_mbuf_cache_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "dpdk_rx_desc") {
        config.dpdk_rx_desc = static_cast<std::uint32_t>(std::stoul(normalized_value));
    } else if (normalized_key == "dpdk_tx_desc") {
        config.dpdk_tx_desc = static_cast<std::uint32_t>(std::stoul(normalized_value));
    }
}

}  // namespace

RxConfig load_default_config() {
    RxConfig config;
    config.cpu_cores = {0};
    return config;
}

RxConfig load_config_file(const std::string& path) {
    RxConfig config = load_default_config();
    if (path.empty()) {
        return config;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open config file: " + path);
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0U, comment_pos);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::size_t separator = line.find(':');
        const std::size_t equals = line.find('=');
        std::size_t split = std::string::npos;
        if (separator != std::string::npos && equals != std::string::npos) {
            split = std::min(separator, equals);
        } else if (separator != std::string::npos) {
            split = separator;
        } else {
            split = equals;
        }

        if (split == std::string::npos) {
            continue;
        }

        assign_config_value(config, line.substr(0U, split), line.substr(split + 1U));
    }

    config.config_path = path;
    return config;
}

void merge_config(RxConfig& base, const RxConfig& overrides) {
    const RxConfig defaults = load_default_config();

    if (!overrides.backend_name.empty()) {
        base.backend_name = overrides.backend_name;
    }
    if (!overrides.mode_name.empty()) {
        base.mode_name = overrides.mode_name;
    }
    if (!overrides.scenario_path.empty()) {
        base.scenario_path = overrides.scenario_path;
    }
    if (!overrides.output_dir.empty()) {
        base.output_dir = overrides.output_dir;
    }
    if (!overrides.interface_name.empty()) {
        base.interface_name = overrides.interface_name;
    }
    if (!overrides.bind_address.empty()) {
        base.bind_address = overrides.bind_address;
    }
    if (!overrides.dpdk_pci_addr.empty()) {
        base.dpdk_pci_addr = overrides.dpdk_pci_addr;
    }
    if (!overrides.xdp_bind_mode.empty()) {
        base.xdp_bind_mode = overrides.xdp_bind_mode;
    }
    if (!overrides.config_path.empty()) {
        base.config_path = overrides.config_path;
    }

    if (overrides.queue_id != defaults.queue_id) {
        base.queue_id = overrides.queue_id;
    }
    if (overrides.max_burst != defaults.max_burst) {
        base.max_burst = overrides.max_burst;
    }
    if (overrides.duration_seconds != 0U) {
        base.duration_seconds = overrides.duration_seconds;
    }
    if (overrides.udp_port != defaults.udp_port) {
        base.udp_port = overrides.udp_port;
    }
    if (overrides.packet_size_bytes != defaults.packet_size_bytes) {
        base.packet_size_bytes = overrides.packet_size_bytes;
    }
    if (overrides.socket_poll_timeout_ms != defaults.socket_poll_timeout_ms) {
        base.socket_poll_timeout_ms = overrides.socket_poll_timeout_ms;
    }
    if (overrides.dpdk_port_id != defaults.dpdk_port_id) {
        base.dpdk_port_id = overrides.dpdk_port_id;
    }
    if (overrides.dpdk_socket_mem_mb != defaults.dpdk_socket_mem_mb) {
        base.dpdk_socket_mem_mb = overrides.dpdk_socket_mem_mb;
    }
    if (overrides.dpdk_mempool_size != defaults.dpdk_mempool_size) {
        base.dpdk_mempool_size = overrides.dpdk_mempool_size;
    }
    if (overrides.dpdk_mbuf_cache_size != defaults.dpdk_mbuf_cache_size) {
        base.dpdk_mbuf_cache_size = overrides.dpdk_mbuf_cache_size;
    }
    if (overrides.dpdk_rx_desc != defaults.dpdk_rx_desc) {
        base.dpdk_rx_desc = overrides.dpdk_rx_desc;
    }
    if (overrides.dpdk_tx_desc != defaults.dpdk_tx_desc) {
        base.dpdk_tx_desc = overrides.dpdk_tx_desc;
    }
    if (overrides.enable_internal_traffic != defaults.enable_internal_traffic) {
        base.enable_internal_traffic = overrides.enable_internal_traffic;
    }
    if (!overrides.cpu_cores.empty()) {
        base.cpu_cores = overrides.cpu_cores;
    }
}

}  // namespace rxtech
