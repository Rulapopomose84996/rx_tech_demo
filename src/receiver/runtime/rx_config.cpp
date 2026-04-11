#include "rxtech/rx_config.h"

#include "rxtech/protocol_spec.h"

#include <cctype>
#include <fstream>
#include <functional>
#include <string>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rxtech
{

    CapturePolicy parse_capture_policy(const std::string &value)
    {
        std::string normalized = value;
        for (char &ch : normalized)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (normalized == "disabled" || normalized == "none")
        {
            return CapturePolicy::disabled;
        }
        if (normalized == "full")
        {
            return CapturePolicy::full;
        }
        return CapturePolicy::first_effective_cpi;
    }

    const char *capture_policy_name(CapturePolicy policy) noexcept
    {
        switch (policy)
        {
        case CapturePolicy::disabled:
            return "disabled";
        case CapturePolicy::first_effective_cpi:
            return "first_effective_cpi";
        case CapturePolicy::full:
            return "full";
        }
        return "first_effective_cpi";
    }

    namespace
    {

        std::string trim(const std::string &value)
        {
            std::size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
            {
                ++start;
            }

            std::size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0)
            {
                --end;
            }

            return value.substr(start, end - start);
        }

        std::string strip_quotes(const std::string &value)
        {
            if (value.size() >= 2U)
            {
                const char first = value.front();
                const char last = value.back();
                if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
                {
                    return value.substr(1U, value.size() - 2U);
                }
            }
            return value;
        }

        std::string to_lower(std::string value)
        {
            for (char &ch : value)
            {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        bool parse_bool(const std::string &value)
        {
            const std::string normalized = to_lower(trim(value));
            return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
        }

        std::vector<int> parse_int_list(const std::string &value)
        {
            std::string normalized = trim(value);
            if (!normalized.empty() && normalized.front() == '[' && normalized.back() == ']')
            {
                normalized = normalized.substr(1U, normalized.size() - 2U);
            }

            std::vector<int> result;
            std::stringstream stream(normalized);
            std::string item;
            while (std::getline(stream, item, ','))
            {
                item = trim(item);
                if (!item.empty())
                {
                    result.push_back(std::stoi(item));
                }
            }
            return result;
        }

        bool should_skip_assignment(const std::string &canonical_key, bool section_key,
                                    std::unordered_set<std::string> &section_assigned_keys)
        {
            if (section_key)
            {
                section_assigned_keys.insert(canonical_key);
                return false;
            }
            return section_assigned_keys.count(canonical_key) > 0U;
        }

        using ConfigSetter = std::function<void(RxConfig &, const std::string &)>;
        using ConfigDispatch = std::unordered_map<std::string, std::pair<std::string, ConfigSetter>>;

        const ConfigDispatch &config_dispatch_map()
        {
            struct Row
            {
                const char *canonical;
                std::vector<const char *> names;
                ConfigSetter setter;
            };

            static const ConfigDispatch map = []()
            {
                const std::vector<Row> table = {
                    {"backend_name",
                     {"backend", "backend_name"},
                     [](RxConfig &c, const std::string &v) { c.process.backend_name = v; }},

                    {"capture_output_dir",
                     {"output", "output_dir", "capture_output_dir", "capture.output_dir"},
                     [](RxConfig &c, const std::string &v)
                     {
                         c.operations.output_dir = v;
                         c.capture.capture_output_dir = v;
                     }},

                    {"capture_enabled",
                     {"capture.enabled", "capture_enabled"},
                     [](RxConfig &c, const std::string &v)
                     {
                         c.capture.capture_enabled = parse_bool(v);
                         c.capture.capture_policy =
                             c.capture.capture_enabled ? c.capture.capture_policy : CapturePolicy::disabled;
                     }},

                    {"capture_policy",
                     {"capture.policy", "capture_policy"},
                     [](RxConfig &c, const std::string &v)
                     {
                         c.capture.capture_policy = parse_capture_policy(v);
                         c.capture.capture_enabled = c.capture.capture_policy != CapturePolicy::disabled;
                     }},

                    {"capture_index_filename",
                     {"capture.index_filename", "capture_index_filename"},
                     [](RxConfig &c, const std::string &v) { c.capture.capture_index_filename = v; }},

                    {"capture_data_filename",
                     {"capture.data_filename", "capture_data_filename"},
                     [](RxConfig &c, const std::string &v) { c.capture.capture_data_filename = v; }},

                    {"raw_record_enabled",
                     {"raw_record.enabled", "raw_record_enabled"},
                     [](RxConfig &c, const std::string &v) { c.capture.raw_record_enabled = parse_bool(v); }},

                    {"raw_record_output_dir",
                     {"raw_record.output_dir", "raw_record_output_dir"},
                     [](RxConfig &c, const std::string &v) { c.capture.raw_record_output_dir = v; }},

                    {"raw_record_file_prefix",
                     {"raw_record.file_prefix", "raw_record_file_prefix"},
                     [](RxConfig &c, const std::string &v) { c.capture.raw_record_file_prefix = v; }},

                    {"raw_record_ring_slots",
                     {"raw_record.ring_slots", "raw_record_ring_slots"},
                     [](RxConfig &c, const std::string &v)
                     { c.capture.raw_record_ring_slots = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"raw_record_writer_batch_size",
                     {"raw_record.writer_batch_size", "raw_record_writer_batch_size"},
                     [](RxConfig &c, const std::string &v)
                     { c.capture.raw_record_writer_batch_size = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"raw_record_max_frame_bytes",
                     {"raw_record.max_frame_bytes", "raw_record_max_frame_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.capture.raw_record_max_frame_bytes = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"raw_record_segment_bytes",
                     {"raw_record.segment_bytes", "raw_record_segment_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.capture.raw_record_segment_bytes = static_cast<std::uint64_t>(std::stoull(v)); }},

                    {"raw_record_max_total_bytes",
                     {"raw_record.max_total_bytes", "raw_record_max_total_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.capture.raw_record_max_total_bytes = static_cast<std::uint64_t>(std::stoull(v)); }},

                    {"interface_name",
                     {"interface", "interface_name", "network.interface_name"},
                     [](RxConfig &c, const std::string &v) { c.ingress.interface_name = v; }},

                    {"receiver_ipv4",
                     {"receiver_ipv4", "network.receiver_ipv4"},
                     [](RxConfig &c, const std::string &v) { c.ingress.receiver_ipv4 = v; }},

                    {"allowed_source_ipv4",
                     {"allowed_source_ipv4", "network.allowed_source_ipv4"},
                     [](RxConfig &c, const std::string &v) { c.ingress.allowed_source_ipv4 = v; }},

                    {"socket_bind_ip",
                     {"socket_bind_ip", "network.socket_bind_ip", "socket.bind_ip"},
                     [](RxConfig &c, const std::string &v) { c.ingress.socket_bind_ip = v; }},

                    {"socket_bind_port",
                     {"socket_bind_port", "network.socket_bind_port", "socket.bind_port"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.socket_bind_port = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"socket_rcvbuf_bytes",
                     {"socket_rcvbuf_bytes", "socket.rcvbuf_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.socket_rcvbuf_bytes = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"socket_nonblocking",
                     {"socket_nonblocking", "socket.nonblocking"},
                     [](RxConfig &c, const std::string &v) { c.ingress.socket_nonblocking = parse_bool(v); }},

                    {"socket_batch_timeout_ms",
                     {"socket_batch_timeout_ms", "socket.batch_timeout_ms"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.socket_batch_timeout_ms = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"queue_id",
                     {"queue_id", "network.queue_id"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.queue_id = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"max_burst",
                     {"max_burst", "runtime.max_burst"},
                     [](RxConfig &c, const std::string &v)
                     { c.runtime.max_burst = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"duration_seconds",
                     {"duration_seconds", "runtime.duration_seconds"},
                     [](RxConfig &c, const std::string &v)
                     { c.runtime.duration_seconds = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"packet_size_bytes",
                     {"packet_size_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.runtime.packet_size_bytes = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"status_interval_seconds",
                     {"status_interval_seconds", "runtime.status_interval_seconds"},
                     [](RxConfig &c, const std::string &v)
                     { c.operations.status_interval_seconds = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"allowed_dest_port",
                     {"allowed_dest_port", "network.allowed_dest_port"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.allowed_dest_port = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"cpu_cores",
                     {"cpu_cores", "runtime.cpu_cores"},
                     [](RxConfig &c, const std::string &v) { c.process.cpu_cores = parse_int_list(v); }},

                    {"run_until_stopped",
                     {"run_until_stopped", "runtime.run_until_stopped"},
                     [](RxConfig &c, const std::string &v) { c.runtime.run_until_stopped = parse_bool(v); }},

                    {"dpdk_port_id",
                     {"dpdk_port_id", "dpdk.port_id"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.dpdk_port_id = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_pci_addr",
                     {"dpdk_pci_addr", "dpdk.pci_addr"},
                     [](RxConfig &c, const std::string &v) { c.ingress.dpdk_pci_addr = v; }},

                    {"dpdk_socket_mem_mb",
                     {"dpdk_socket_mem_mb", "dpdk.socket_mem_mb"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.dpdk_socket_mem_mb = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_mempool_size",
                     {"dpdk_mempool_size", "dpdk.mempool_size"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.dpdk_mempool_size = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_mbuf_cache_size",
                     {"dpdk_mbuf_cache_size", "dpdk.mbuf_cache_size"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.dpdk_mbuf_cache_size = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_rx_desc",
                     {"dpdk_rx_desc", "dpdk.rx_desc"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.dpdk_rx_desc = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_tx_desc",
                     {"dpdk_tx_desc", "dpdk.tx_desc"},
                     [](RxConfig &c, const std::string &v)
                     { c.ingress.dpdk_tx_desc = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"log_level",
                     {"log.level", "log_level"},
                     [](RxConfig &c, const std::string &v) { c.operations.log_level = to_lower(v); }},

                    {"log_output",
                     {"log.output", "log_output"},
                     [](RxConfig &c, const std::string &v) { c.operations.log_output = to_lower(v); }},

                    {"log_file_path",
                     {"log.file_path", "log_file_path"},
                     [](RxConfig &c, const std::string &v) { c.operations.log_file_path = v; }},

                    {"structured_log_output",
                     {"log.structured_output", "structured_log_output"},
                     [](RxConfig &c, const std::string &v) { c.operations.structured_log_output = to_lower(v); }},

                    {"structured_log_file_path",
                     {"log.structured_file_path", "structured_log_file_path"},
                     [](RxConfig &c, const std::string &v) { c.operations.structured_log_file_path = v; }},

                    {"structured_log_format",
                     {"log.structured_format", "structured_log_format"},
                     [](RxConfig &c, const std::string &v) { c.operations.structured_log_format = to_lower(v); }},

                    {"log_rate_limit_seconds",
                     {"log.rate_limit_seconds", "log_rate_limit_seconds"},
                     [](RxConfig &c, const std::string &v)
                     { c.operations.log_rate_limit_seconds = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_udp_packet_size",
                     {"protocol.udp_packet_size", "protocol_udp_packet_size"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol.udp_packet_size = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_channels_per_prt",
                     {"protocol.channels_per_prt", "protocol_channels_per_prt"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol.channels_per_prt = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_packets_per_channel",
                     {"protocol.packets_per_channel", "protocol_packets_per_channel"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol.packets_per_channel = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_expected_n_prt",
                     {"protocol.expected_n_prt", "protocol_expected_n_prt"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol.expected_n_prt = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_cpi_timeout_ns",
                     {"protocol.cpi_timeout_ns", "protocol_cpi_timeout_ns"},
                     [](RxConfig &c, const std::string &v) { c.protocol.cpi_timeout_ns = std::stoull(v); }},

                    {"protocol_dynamic_prt_enabled",
                     {"protocol.dynamic_prt_enabled", "protocol_dynamic_prt_enabled"},
                     [](RxConfig &c, const std::string &v) { c.protocol.dynamic_prt_enabled = parse_bool(v); }},

                    {"protocol_max_n_prt",
                     {"protocol.max_n_prt", "protocol_max_n_prt"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol.max_n_prt = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"output_drop_policy",
                     {"output_drop_policy", "runtime.output_drop_policy"},
                     [](RxConfig &c, const std::string &v)
                     { c.operations.output_drop_policy = parse_output_drop_policy(v); }},

                    {"output_ring_capacity",
                     {"output_ring_capacity", "runtime.output_ring_capacity"},
                     [](RxConfig &c, const std::string &v)
                     { c.operations.output_ring_capacity = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"recycle_ring_capacity",
                     {"recycle_ring_capacity", "runtime.recycle_ring_capacity"},
                     [](RxConfig &c, const std::string &v)
                     { c.operations.recycle_ring_capacity = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"metrics_export_mode",
                     {"metrics.export_mode", "metrics_export_mode"},
                     [](RxConfig &c, const std::string &v) { c.operations.metrics_export_mode = to_lower(v); }},

                    {"metrics_export_path",
                     {"metrics.export_path", "metrics_export_path"},
                     [](RxConfig &c, const std::string &v) { c.operations.metrics_export_path = v; }},

                    {"metrics_export_interval_seconds",
                     {"metrics.export_interval_seconds", "metrics_export_interval_seconds"},
                     [](RxConfig &c, const std::string &v)
                     { c.operations.metrics_export_interval_seconds = static_cast<std::uint32_t>(std::stoul(v)); }},
                };

                ConfigDispatch m;
                for (const auto &row : table)
                {
                    for (const char *name : row.names)
                    {
                        m.emplace(name, std::make_pair(std::string(row.canonical), row.setter));
                    }
                }
                return m;
            }();
            return map;
        }

        void assign_config_value(RxConfig &config, const std::string &key, const std::string &value, bool section_key,
                                 std::unordered_set<std::string> &section_assigned_keys)
        {
            const std::string nk = to_lower(trim(key));
            const std::string nv = strip_quotes(trim(value));

            const ConfigDispatch &dispatch = config_dispatch_map();
            const auto it = dispatch.find(nk);
            if (it == dispatch.end())
            {
                return;
            }
            const std::string &canonical = it->second.first;
            if (should_skip_assignment(canonical, section_key, section_assigned_keys))
            {
                return;
            }
            it->second.second(config, nv);
        }

    } // namespace

    OutputDropPolicy parse_output_drop_policy(const std::string &value)
    {
        const std::string normalized = to_lower(trim(value));
        return normalized == "error" ? OutputDropPolicy::error : OutputDropPolicy::degrade;
    }

    const char *output_drop_policy_name(OutputDropPolicy policy) noexcept
    {
        switch (policy)
        {
        case OutputDropPolicy::error:
            return "error";
        case OutputDropPolicy::degrade:
        default:
            return "degrade";
        }
    }

    RxConfig load_default_config()
    {
        return RxConfig{};
    }

    RxConfig load_config_file(const std::string &path)
    {
        RxConfig config = load_default_config();
        if (path.empty())
        {
            return config;
        }

        std::ifstream input(path);
        if (!input.is_open())
        {
            throw std::runtime_error("打开配置文件失败: " + path);
        }

        std::string line;
        std::string current_section;
        std::unordered_set<std::string> section_assigned_keys;
        while (std::getline(input, line))
        {
            const std::size_t comment_pos = line.find('#');
            if (comment_pos != std::string::npos)
            {
                line = line.substr(0U, comment_pos);
            }

            line = trim(line);
            if (line.empty())
            {
                continue;
            }

            if (line.size() >= 2U && line.front() == '[' && line.back() == ']')
            {
                current_section = to_lower(trim(line.substr(1U, line.size() - 2U)));
                continue;
            }

            const std::size_t separator = line.find(':');
            const std::size_t equals = line.find('=');
            std::size_t split = std::string::npos;
            if (separator != std::string::npos && equals != std::string::npos)
            {
                split = std::min(separator, equals);
            }
            else if (separator != std::string::npos)
            {
                split = separator;
            }
            else
            {
                split = equals;
            }

            if (split == std::string::npos)
            {
                continue;
            }

            const std::string key = trim(line.substr(0U, split));
            const bool section_key = !current_section.empty();
            const std::string effective_key = section_key ? (current_section + "." + key) : key;
            assign_config_value(config, effective_key, line.substr(split + 1U), section_key, section_assigned_keys);
        }

        if (config.capture.capture_output_dir.empty())
        {
            config.capture.capture_output_dir = config.operations.output_dir;
        }
        config.capture.capture_policy =
            config.capture.capture_enabled ? config.capture.capture_policy : CapturePolicy::disabled;
        if (config.operations.output_dir.empty())
        {
            config.operations.output_dir = config.capture.capture_output_dir;
        }
        config.process.config_path = path;
        return config;
    }

    void merge_config(RxConfig &base, const RxConfig &overrides)
    {
        const RxConfig defaults = load_default_config();

        if (!overrides.process.backend_name.empty())
            base.process.backend_name = overrides.process.backend_name;
        if (!overrides.operations.output_dir.empty())
            base.operations.output_dir = overrides.operations.output_dir;
        if (!overrides.capture.capture_output_dir.empty())
            base.capture.capture_output_dir = overrides.capture.capture_output_dir;
        if (!overrides.capture.capture_index_filename.empty())
            base.capture.capture_index_filename = overrides.capture.capture_index_filename;
        if (!overrides.capture.capture_data_filename.empty())
            base.capture.capture_data_filename = overrides.capture.capture_data_filename;
        if (!overrides.capture.raw_record_output_dir.empty())
            base.capture.raw_record_output_dir = overrides.capture.raw_record_output_dir;
        if (!overrides.capture.raw_record_file_prefix.empty())
            base.capture.raw_record_file_prefix = overrides.capture.raw_record_file_prefix;
        if (!overrides.ingress.interface_name.empty())
            base.ingress.interface_name = overrides.ingress.interface_name;
        if (!overrides.ingress.receiver_ipv4.empty())
            base.ingress.receiver_ipv4 = overrides.ingress.receiver_ipv4;
        if (!overrides.ingress.allowed_source_ipv4.empty())
            base.ingress.allowed_source_ipv4 = overrides.ingress.allowed_source_ipv4;
        if (!overrides.ingress.socket_bind_ip.empty())
            base.ingress.socket_bind_ip = overrides.ingress.socket_bind_ip;
        if (!overrides.ingress.dpdk_pci_addr.empty())
            base.ingress.dpdk_pci_addr = overrides.ingress.dpdk_pci_addr;
        if (!overrides.operations.log_level.empty())
            base.operations.log_level = overrides.operations.log_level;
        if (!overrides.operations.log_output.empty())
            base.operations.log_output = overrides.operations.log_output;
        if (!overrides.operations.log_file_path.empty())
            base.operations.log_file_path = overrides.operations.log_file_path;
        if (!overrides.operations.structured_log_output.empty())
            base.operations.structured_log_output = overrides.operations.structured_log_output;
        if (!overrides.operations.structured_log_file_path.empty())
            base.operations.structured_log_file_path = overrides.operations.structured_log_file_path;
        if (!overrides.operations.structured_log_format.empty())
            base.operations.structured_log_format = overrides.operations.structured_log_format;
        if (!overrides.operations.metrics_export_mode.empty())
            base.operations.metrics_export_mode = overrides.operations.metrics_export_mode;
        if (!overrides.operations.metrics_export_path.empty())
            base.operations.metrics_export_path = overrides.operations.metrics_export_path;
        if (!overrides.process.config_path.empty())
            base.process.config_path = overrides.process.config_path;

        if (overrides.ingress.queue_id != defaults.ingress.queue_id)
            base.ingress.queue_id = overrides.ingress.queue_id;
        if (overrides.runtime.max_burst != defaults.runtime.max_burst)
            base.runtime.max_burst = overrides.runtime.max_burst;
        if (overrides.runtime.duration_seconds != 0U)
            base.runtime.duration_seconds = overrides.runtime.duration_seconds;
        if (overrides.runtime.packet_size_bytes != defaults.runtime.packet_size_bytes)
            base.runtime.packet_size_bytes = overrides.runtime.packet_size_bytes;
        if (overrides.operations.status_interval_seconds != defaults.operations.status_interval_seconds)
            base.operations.status_interval_seconds = overrides.operations.status_interval_seconds;
        if (overrides.ingress.allowed_dest_port != defaults.ingress.allowed_dest_port)
            base.ingress.allowed_dest_port = overrides.ingress.allowed_dest_port;
        if (overrides.ingress.socket_bind_port != defaults.ingress.socket_bind_port)
            base.ingress.socket_bind_port = overrides.ingress.socket_bind_port;
        if (overrides.ingress.socket_rcvbuf_bytes != defaults.ingress.socket_rcvbuf_bytes)
            base.ingress.socket_rcvbuf_bytes = overrides.ingress.socket_rcvbuf_bytes;
        if (overrides.ingress.socket_nonblocking != defaults.ingress.socket_nonblocking)
            base.ingress.socket_nonblocking = overrides.ingress.socket_nonblocking;
        if (overrides.ingress.socket_batch_timeout_ms != defaults.ingress.socket_batch_timeout_ms)
            base.ingress.socket_batch_timeout_ms = overrides.ingress.socket_batch_timeout_ms;
        if (overrides.capture.capture_enabled != defaults.capture.capture_enabled)
            base.capture.capture_enabled = overrides.capture.capture_enabled;
        if (overrides.capture.capture_policy != defaults.capture.capture_policy)
            base.capture.capture_policy = overrides.capture.capture_policy;
        if (overrides.capture.raw_record_enabled != defaults.capture.raw_record_enabled)
            base.capture.raw_record_enabled = overrides.capture.raw_record_enabled;
        if (overrides.ingress.dpdk_port_id != defaults.ingress.dpdk_port_id)
            base.ingress.dpdk_port_id = overrides.ingress.dpdk_port_id;
        if (overrides.ingress.dpdk_socket_mem_mb != defaults.ingress.dpdk_socket_mem_mb)
            base.ingress.dpdk_socket_mem_mb = overrides.ingress.dpdk_socket_mem_mb;
        if (overrides.ingress.dpdk_mempool_size != defaults.ingress.dpdk_mempool_size)
            base.ingress.dpdk_mempool_size = overrides.ingress.dpdk_mempool_size;
        if (overrides.ingress.dpdk_mbuf_cache_size != defaults.ingress.dpdk_mbuf_cache_size)
            base.ingress.dpdk_mbuf_cache_size = overrides.ingress.dpdk_mbuf_cache_size;
        if (overrides.ingress.dpdk_rx_desc != defaults.ingress.dpdk_rx_desc)
            base.ingress.dpdk_rx_desc = overrides.ingress.dpdk_rx_desc;
        if (overrides.ingress.dpdk_tx_desc != defaults.ingress.dpdk_tx_desc)
            base.ingress.dpdk_tx_desc = overrides.ingress.dpdk_tx_desc;
        if (overrides.protocol.udp_packet_size != defaults.protocol.udp_packet_size)
        {
            base.protocol.udp_packet_size = overrides.protocol.udp_packet_size;
        }
        if (overrides.protocol.channels_per_prt != defaults.protocol.channels_per_prt)
        {
            base.protocol.channels_per_prt = overrides.protocol.channels_per_prt;
        }
        if (overrides.protocol.packets_per_channel != defaults.protocol.packets_per_channel)
        {
            base.protocol.packets_per_channel = overrides.protocol.packets_per_channel;
        }
        if (overrides.protocol.expected_n_prt != defaults.protocol.expected_n_prt)
        {
            base.protocol.expected_n_prt = overrides.protocol.expected_n_prt;
        }
        if (overrides.protocol.cpi_timeout_ns != defaults.protocol.cpi_timeout_ns)
        {
            base.protocol.cpi_timeout_ns = overrides.protocol.cpi_timeout_ns;
        }
        if (overrides.protocol.dynamic_prt_enabled != defaults.protocol.dynamic_prt_enabled)
        {
            base.protocol.dynamic_prt_enabled = overrides.protocol.dynamic_prt_enabled;
        }
        if (overrides.protocol.max_n_prt != defaults.protocol.max_n_prt)
        {
            base.protocol.max_n_prt = overrides.protocol.max_n_prt;
        }
        if (overrides.capture.raw_record_ring_slots != defaults.capture.raw_record_ring_slots)
            base.capture.raw_record_ring_slots = overrides.capture.raw_record_ring_slots;
        if (overrides.capture.raw_record_writer_batch_size != defaults.capture.raw_record_writer_batch_size)
            base.capture.raw_record_writer_batch_size = overrides.capture.raw_record_writer_batch_size;
        if (overrides.capture.raw_record_max_frame_bytes != defaults.capture.raw_record_max_frame_bytes)
            base.capture.raw_record_max_frame_bytes = overrides.capture.raw_record_max_frame_bytes;
        if (overrides.capture.raw_record_segment_bytes != defaults.capture.raw_record_segment_bytes)
            base.capture.raw_record_segment_bytes = overrides.capture.raw_record_segment_bytes;
        if (overrides.capture.raw_record_max_total_bytes != defaults.capture.raw_record_max_total_bytes)
            base.capture.raw_record_max_total_bytes = overrides.capture.raw_record_max_total_bytes;
        if (overrides.runtime.run_until_stopped != defaults.runtime.run_until_stopped)
            base.runtime.run_until_stopped = overrides.runtime.run_until_stopped;
        if (!overrides.process.cpu_cores.empty())
            base.process.cpu_cores = overrides.process.cpu_cores;
        if (overrides.operations.output_drop_policy != defaults.operations.output_drop_policy)
            base.operations.output_drop_policy = overrides.operations.output_drop_policy;
        if (overrides.operations.output_ring_capacity != defaults.operations.output_ring_capacity)
            base.operations.output_ring_capacity = overrides.operations.output_ring_capacity;
        if (overrides.operations.recycle_ring_capacity != defaults.operations.recycle_ring_capacity)
            base.operations.recycle_ring_capacity = overrides.operations.recycle_ring_capacity;
        if (overrides.operations.log_rate_limit_seconds != defaults.operations.log_rate_limit_seconds)
            base.operations.log_rate_limit_seconds = overrides.operations.log_rate_limit_seconds;
        if (overrides.operations.metrics_export_interval_seconds != defaults.operations.metrics_export_interval_seconds)
            base.operations.metrics_export_interval_seconds = overrides.operations.metrics_export_interval_seconds;
    }

    std::string effective_socket_bind_ip(const RxConfig &config)
    {
        return config.ingress.socket_bind_ip.empty() ? config.ingress.receiver_ipv4 : config.ingress.socket_bind_ip;
    }

    std::uint16_t effective_socket_bind_port(const RxConfig &config)
    {
        const std::uint32_t port =
            config.ingress.socket_bind_port != 0U ? config.ingress.socket_bind_port : config.ingress.allowed_dest_port;
        if (port == 0U || port > 65535U)
        {
            return 0U;
        }
        return static_cast<std::uint16_t>(port);
    }

    std::vector<std::string> validate_config(const RxConfig &config)
    {
        constexpr std::uint32_t kMaxUdpPort = 65535U;
        std::vector<std::string> errors;

        if (config.ingress.allowed_dest_port > kMaxUdpPort)
        {
            errors.emplace_back("allowed_dest_port 必须小于或等于 65535");
        }

        if (config.process.backend_name == "socket")
        {
            const std::uint32_t raw_socket_bind_port = config.ingress.socket_bind_port != 0U
                                                           ? config.ingress.socket_bind_port
                                                           : config.ingress.allowed_dest_port;
            const std::string socket_bind_ip = effective_socket_bind_ip(config);
            const std::uint16_t socket_bind_port = effective_socket_bind_port(config);
            if (socket_bind_ip.empty())
            {
                errors.emplace_back("backend=socket 时，socket_bind_ip 或 receiver_ipv4 不能为空");
            }
            if (config.ingress.socket_bind_port > kMaxUdpPort)
            {
                errors.emplace_back("backend=socket 时，socket_bind_port 必须小于或等于 65535");
            }
            if (raw_socket_bind_port == 0U || socket_bind_port == 0U)
            {
                errors.emplace_back("backend=socket 时，socket_bind_port 或 allowed_dest_port 必须大于 0");
            }
            if (!config.ingress.socket_bind_ip.empty() && config.ingress.socket_bind_ip != "0.0.0.0" &&
                !config.ingress.receiver_ipv4.empty() && config.ingress.socket_bind_ip != config.ingress.receiver_ipv4)
            {
                errors.emplace_back(
                    "backend=socket 时，socket_bind_ip 与 receiver_ipv4 不能冲突；如需任意地址绑定请使用 0.0.0.0");
            }
            if (config.ingress.socket_bind_port != 0U && config.ingress.allowed_dest_port != 0U &&
                config.ingress.socket_bind_port != config.ingress.allowed_dest_port)
            {
                errors.emplace_back("backend=socket 时，socket_bind_port 与 allowed_dest_port 不能冲突");
            }
            if (config.ingress.socket_rcvbuf_bytes == 0U)
            {
                errors.emplace_back("backend=socket 时，socket_rcvbuf_bytes 必须大于 0");
            }
        }

        if (config.capture.capture_policy != CapturePolicy::disabled)
        {
            const std::string capture_output_dir = config.capture.capture_output_dir.empty()
                                                       ? config.operations.output_dir
                                                       : config.capture.capture_output_dir;
            if (capture_output_dir.empty())
            {
                errors.emplace_back("启用数据捕获时，capture_output_dir 不能为空");
            }
            if (config.capture.capture_index_filename.empty())
            {
                errors.emplace_back("启用数据捕获时，capture_index_filename 不能为空");
            }
            if (config.capture.capture_data_filename.empty())
            {
                errors.emplace_back("启用数据捕获时，capture_data_filename 不能为空");
            }
        }

        if (config.capture.raw_record_enabled)
        {
            if (config.capture.raw_record_output_dir.empty())
            {
                errors.emplace_back("启用原始记录时，raw_record_output_dir 不能为空");
            }
            if (config.capture.raw_record_file_prefix.empty())
            {
                errors.emplace_back("启用原始记录时，raw_record_file_prefix 不能为空");
            }
            if (config.capture.raw_record_ring_slots == 0U)
            {
                errors.emplace_back("raw_record_ring_slots 必须大于 0");
            }
            if (config.capture.raw_record_writer_batch_size == 0U)
            {
                errors.emplace_back("raw_record_writer_batch_size 必须大于 0");
            }
            if (config.capture.raw_record_max_frame_bytes == 0U)
            {
                errors.emplace_back("raw_record_max_frame_bytes 必须大于 0");
            }
            if (config.capture.raw_record_segment_bytes == 0U)
            {
                errors.emplace_back("raw_record_segment_bytes 必须大于 0");
            }
            if (config.capture.raw_record_max_total_bytes == 0U)
            {
                errors.emplace_back("raw_record_max_total_bytes 必须大于 0");
            }
            if (config.capture.raw_record_segment_bytes > config.capture.raw_record_max_total_bytes)
            {
                errors.emplace_back("raw_record_segment_bytes 必须小于或等于 raw_record_max_total_bytes");
            }
        }

        if (config.operations.log_output == "file" && config.operations.log_file_path.empty())
        {
            errors.emplace_back("当日志输出模式为 file 时，log_file_path 不能为空");
        }
        if (config.operations.structured_log_output == "file" && config.operations.structured_log_file_path.empty())
        {
            errors.emplace_back("当 structured_log_output 为 file 时，structured_log_file_path 不能为空");
        }
        if (config.operations.structured_log_output != "disabled" &&
            config.operations.structured_log_output != "stdout" &&
            config.operations.structured_log_output != "stderr" && config.operations.structured_log_output != "file")
        {
            errors.emplace_back("structured_log_output 仅支持 disabled/stdout/stderr/file");
        }
        if (config.operations.structured_log_format != "json" && config.operations.structured_log_format != "text")
        {
            errors.emplace_back("structured_log_format 仅支持 json 或 text");
        }
        if (config.operations.metrics_export_mode != "none" &&
            config.operations.metrics_export_mode != "prometheus_text" &&
            config.operations.metrics_export_mode != "json_socket")
        {
            errors.emplace_back("metrics_export_mode 仅支持 none/prometheus_text/json_socket");
        }
        if (config.operations.metrics_export_mode != "none" && config.operations.metrics_export_path.empty())
        {
            errors.emplace_back("启用 metrics_export_mode 时，metrics_export_path 不能为空");
        }
        if (config.protocol.udp_packet_size == 0U)
        {
            errors.emplace_back("protocol_udp_packet_size 必须大于 0");
        }
        if (config.protocol.channels_per_prt == 0U)
        {
            errors.emplace_back("protocol_channels_per_prt 必须大于 0");
        }
        if (config.protocol.packets_per_channel == 0U)
        {
            errors.emplace_back("protocol_packets_per_channel 必须大于 0");
        }

        return errors;
    }

    ProtocolSpec protocol_spec_from_config(const RxConfig &config)
    {
        ProtocolSpec spec;
        spec.udp_packet_size = config.protocol.udp_packet_size;
        spec.channels_per_prt = config.protocol.channels_per_prt > 0U && config.protocol.channels_per_prt <= 4U
                                    ? config.protocol.channels_per_prt
                                    : 3U;
        spec.packets_per_channel = config.protocol.packets_per_channel > 0U && config.protocol.packets_per_channel <= 9U
                                       ? config.protocol.packets_per_channel
                                       : 9U;
        spec.packet_data_size =
            spec.udp_packet_size > spec.packet_header_size ? (spec.udp_packet_size - spec.packet_header_size) : 0U;
        spec.control_table_size = spec.udp_packet_size;
        spec.expected_n_prt = config.protocol.expected_n_prt;
        spec.protocol_cpi_timeout_ns = config.protocol.cpi_timeout_ns;
        spec.dynamic_prt_enabled = config.protocol.dynamic_prt_enabled;
        spec.max_n_prt = config.protocol.max_n_prt;
        return spec;
    }

} // namespace rxtech
