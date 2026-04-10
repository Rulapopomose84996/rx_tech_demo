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
                     [](RxConfig &c, const std::string &v) { c.backend_name = v; }},

                    {"capture_output_dir",
                     {"output", "output_dir", "capture_output_dir", "capture.output_dir"},
                     [](RxConfig &c, const std::string &v)
                     {
                         c.output_dir = v;
                         c.capture_output_dir = v;
                     }},

                    {"capture_enabled",
                     {"capture.enabled", "capture_enabled"},
                     [](RxConfig &c, const std::string &v) { c.capture_enabled = parse_bool(v); }},

                    {"capture_index_filename",
                     {"capture.index_filename", "capture_index_filename"},
                     [](RxConfig &c, const std::string &v) { c.capture_index_filename = v; }},

                    {"capture_data_filename",
                     {"capture.data_filename", "capture_data_filename"},
                     [](RxConfig &c, const std::string &v) { c.capture_data_filename = v; }},

                    {"raw_record_enabled",
                     {"raw_record.enabled", "raw_record_enabled"},
                     [](RxConfig &c, const std::string &v) { c.raw_record_enabled = parse_bool(v); }},

                    {"raw_record_output_dir",
                     {"raw_record.output_dir", "raw_record_output_dir"},
                     [](RxConfig &c, const std::string &v) { c.raw_record_output_dir = v; }},

                    {"raw_record_file_prefix",
                     {"raw_record.file_prefix", "raw_record_file_prefix"},
                     [](RxConfig &c, const std::string &v) { c.raw_record_file_prefix = v; }},

                    {"raw_record_ring_slots",
                     {"raw_record.ring_slots", "raw_record_ring_slots"},
                     [](RxConfig &c, const std::string &v)
                     { c.raw_record_ring_slots = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"raw_record_writer_batch_size",
                     {"raw_record.writer_batch_size", "raw_record_writer_batch_size"},
                     [](RxConfig &c, const std::string &v)
                     { c.raw_record_writer_batch_size = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"raw_record_max_frame_bytes",
                     {"raw_record.max_frame_bytes", "raw_record_max_frame_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.raw_record_max_frame_bytes = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"raw_record_segment_bytes",
                     {"raw_record.segment_bytes", "raw_record_segment_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.raw_record_segment_bytes = static_cast<std::uint64_t>(std::stoull(v)); }},

                    {"raw_record_max_total_bytes",
                     {"raw_record.max_total_bytes", "raw_record_max_total_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.raw_record_max_total_bytes = static_cast<std::uint64_t>(std::stoull(v)); }},

                    {"interface_name",
                     {"interface", "interface_name", "network.interface_name"},
                     [](RxConfig &c, const std::string &v) { c.interface_name = v; }},

                    {"receiver_ipv4",
                     {"receiver_ipv4", "network.receiver_ipv4"},
                     [](RxConfig &c, const std::string &v) { c.receiver_ipv4 = v; }},

                    {"allowed_source_ipv4",
                     {"allowed_source_ipv4", "network.allowed_source_ipv4"},
                     [](RxConfig &c, const std::string &v) { c.allowed_source_ipv4 = v; }},

                    {"socket_bind_ip",
                     {"socket_bind_ip", "network.socket_bind_ip", "socket.bind_ip"},
                     [](RxConfig &c, const std::string &v) { c.socket_bind_ip = v; }},

                    {"socket_bind_port",
                     {"socket_bind_port", "network.socket_bind_port", "socket.bind_port"},
                     [](RxConfig &c, const std::string &v)
                     { c.socket_bind_port = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"socket_rcvbuf_bytes",
                     {"socket_rcvbuf_bytes", "socket.rcvbuf_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.socket_rcvbuf_bytes = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"socket_nonblocking",
                     {"socket_nonblocking", "socket.nonblocking"},
                     [](RxConfig &c, const std::string &v) { c.socket_nonblocking = parse_bool(v); }},

                    {"socket_batch_timeout_ms",
                     {"socket_batch_timeout_ms", "socket.batch_timeout_ms"},
                     [](RxConfig &c, const std::string &v)
                     { c.socket_batch_timeout_ms = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"queue_id",
                     {"queue_id", "network.queue_id"},
                     [](RxConfig &c, const std::string &v) { c.queue_id = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"max_burst",
                     {"max_burst", "runtime.max_burst"},
                     [](RxConfig &c, const std::string &v)
                     { c.max_burst = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"duration_seconds",
                     {"duration_seconds", "runtime.duration_seconds"},
                     [](RxConfig &c, const std::string &v)
                     { c.duration_seconds = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"packet_size_bytes",
                     {"packet_size_bytes"},
                     [](RxConfig &c, const std::string &v)
                     { c.packet_size_bytes = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"status_interval_seconds",
                     {"status_interval_seconds", "runtime.status_interval_seconds"},
                     [](RxConfig &c, const std::string &v)
                     { c.status_interval_seconds = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"allowed_dest_port",
                     {"allowed_dest_port", "network.allowed_dest_port"},
                     [](RxConfig &c, const std::string &v)
                     { c.allowed_dest_port = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"cpu_cores",
                     {"cpu_cores", "runtime.cpu_cores"},
                     [](RxConfig &c, const std::string &v) { c.cpu_cores = parse_int_list(v); }},

                    {"run_until_stopped",
                     {"run_until_stopped", "runtime.run_until_stopped"},
                     [](RxConfig &c, const std::string &v) { c.run_until_stopped = parse_bool(v); }},

                    {"dpdk_port_id",
                     {"dpdk_port_id", "dpdk.port_id"},
                     [](RxConfig &c, const std::string &v)
                     { c.dpdk_port_id = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_pci_addr",
                     {"dpdk_pci_addr", "dpdk.pci_addr"},
                     [](RxConfig &c, const std::string &v) { c.dpdk_pci_addr = v; }},

                    {"dpdk_socket_mem_mb",
                     {"dpdk_socket_mem_mb", "dpdk.socket_mem_mb"},
                     [](RxConfig &c, const std::string &v)
                     { c.dpdk_socket_mem_mb = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_mempool_size",
                     {"dpdk_mempool_size", "dpdk.mempool_size"},
                     [](RxConfig &c, const std::string &v)
                     { c.dpdk_mempool_size = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_mbuf_cache_size",
                     {"dpdk_mbuf_cache_size", "dpdk.mbuf_cache_size"},
                     [](RxConfig &c, const std::string &v)
                     { c.dpdk_mbuf_cache_size = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_rx_desc",
                     {"dpdk_rx_desc", "dpdk.rx_desc"},
                     [](RxConfig &c, const std::string &v)
                     { c.dpdk_rx_desc = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"dpdk_tx_desc",
                     {"dpdk_tx_desc", "dpdk.tx_desc"},
                     [](RxConfig &c, const std::string &v)
                     { c.dpdk_tx_desc = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"log_level",
                     {"log.level", "log_level"},
                     [](RxConfig &c, const std::string &v) { c.log_level = to_lower(v); }},

                    {"log_output",
                     {"log.output", "log_output"},
                     [](RxConfig &c, const std::string &v) { c.log_output = to_lower(v); }},

                    {"log_file_path",
                     {"log.file_path", "log_file_path"},
                     [](RxConfig &c, const std::string &v) { c.log_file_path = v; }},

                    {"protocol_udp_packet_size",
                     {"protocol.udp_packet_size", "protocol_udp_packet_size"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol_udp_packet_size = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_channels_per_prt",
                     {"protocol.channels_per_prt", "protocol_channels_per_prt"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol_channels_per_prt = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_packets_per_channel",
                     {"protocol.packets_per_channel", "protocol_packets_per_channel"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol_packets_per_channel = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_expected_n_prt",
                     {"protocol.expected_n_prt", "protocol_expected_n_prt"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol_expected_n_prt = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"protocol_cpi_timeout_ns",
                     {"protocol.cpi_timeout_ns", "protocol_cpi_timeout_ns"},
                     [](RxConfig &c, const std::string &v) { c.protocol_cpi_timeout_ns = std::stoull(v); }},

                    {"protocol_dynamic_prt_enabled",
                     {"protocol.dynamic_prt_enabled", "protocol_dynamic_prt_enabled"},
                     [](RxConfig &c, const std::string &v) { c.protocol_dynamic_prt_enabled = parse_bool(v); }},

                    {"protocol_max_n_prt",
                     {"protocol.max_n_prt", "protocol_max_n_prt"},
                     [](RxConfig &c, const std::string &v)
                     { c.protocol_max_n_prt = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"output_drop_policy",
                     {"output_drop_policy", "runtime.output_drop_policy"},
                     [](RxConfig &c, const std::string &v) { c.output_drop_policy = parse_output_drop_policy(v); }},

                    {"output_ring_capacity",
                     {"output_ring_capacity", "runtime.output_ring_capacity"},
                     [](RxConfig &c, const std::string &v)
                     { c.output_ring_capacity = static_cast<std::uint32_t>(std::stoul(v)); }},

                    {"recycle_ring_capacity",
                     {"recycle_ring_capacity", "runtime.recycle_ring_capacity"},
                     [](RxConfig &c, const std::string &v)
                     { c.recycle_ring_capacity = static_cast<std::uint32_t>(std::stoul(v)); }},
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
        RxConfig config;
        config.cpu_cores = {0};
        return config;
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

        if (config.capture_output_dir.empty())
        {
            config.capture_output_dir = config.output_dir;
        }
        if (config.output_dir.empty())
        {
            config.output_dir = config.capture_output_dir;
        }
        config.config_path = path;
        return config;
    }

    void merge_config(RxConfig &base, const RxConfig &overrides)
    {
        const RxConfig defaults = load_default_config();

        if (!overrides.backend_name.empty())
            base.backend_name = overrides.backend_name;
        if (!overrides.output_dir.empty())
            base.output_dir = overrides.output_dir;
        if (!overrides.capture_output_dir.empty())
            base.capture_output_dir = overrides.capture_output_dir;
        if (!overrides.capture_index_filename.empty())
            base.capture_index_filename = overrides.capture_index_filename;
        if (!overrides.capture_data_filename.empty())
            base.capture_data_filename = overrides.capture_data_filename;
        if (!overrides.raw_record_output_dir.empty())
            base.raw_record_output_dir = overrides.raw_record_output_dir;
        if (!overrides.raw_record_file_prefix.empty())
            base.raw_record_file_prefix = overrides.raw_record_file_prefix;
        if (!overrides.interface_name.empty())
            base.interface_name = overrides.interface_name;
        if (!overrides.receiver_ipv4.empty())
            base.receiver_ipv4 = overrides.receiver_ipv4;
        if (!overrides.allowed_source_ipv4.empty())
            base.allowed_source_ipv4 = overrides.allowed_source_ipv4;
        if (!overrides.socket_bind_ip.empty())
            base.socket_bind_ip = overrides.socket_bind_ip;
        if (!overrides.dpdk_pci_addr.empty())
            base.dpdk_pci_addr = overrides.dpdk_pci_addr;
        if (!overrides.log_level.empty())
            base.log_level = overrides.log_level;
        if (!overrides.log_output.empty())
            base.log_output = overrides.log_output;
        if (!overrides.log_file_path.empty())
            base.log_file_path = overrides.log_file_path;
        if (!overrides.config_path.empty())
            base.config_path = overrides.config_path;

        if (overrides.queue_id != defaults.queue_id)
            base.queue_id = overrides.queue_id;
        if (overrides.max_burst != defaults.max_burst)
            base.max_burst = overrides.max_burst;
        if (overrides.duration_seconds != 0U)
            base.duration_seconds = overrides.duration_seconds;
        if (overrides.packet_size_bytes != defaults.packet_size_bytes)
            base.packet_size_bytes = overrides.packet_size_bytes;
        if (overrides.status_interval_seconds != defaults.status_interval_seconds)
            base.status_interval_seconds = overrides.status_interval_seconds;
        if (overrides.allowed_dest_port != defaults.allowed_dest_port)
            base.allowed_dest_port = overrides.allowed_dest_port;
        if (overrides.socket_bind_port != defaults.socket_bind_port)
            base.socket_bind_port = overrides.socket_bind_port;
        if (overrides.socket_rcvbuf_bytes != defaults.socket_rcvbuf_bytes)
            base.socket_rcvbuf_bytes = overrides.socket_rcvbuf_bytes;
        if (overrides.socket_nonblocking != defaults.socket_nonblocking)
            base.socket_nonblocking = overrides.socket_nonblocking;
        if (overrides.socket_batch_timeout_ms != defaults.socket_batch_timeout_ms)
            base.socket_batch_timeout_ms = overrides.socket_batch_timeout_ms;
        if (overrides.capture_enabled != defaults.capture_enabled)
            base.capture_enabled = overrides.capture_enabled;
        if (overrides.raw_record_enabled != defaults.raw_record_enabled)
            base.raw_record_enabled = overrides.raw_record_enabled;
        if (overrides.dpdk_port_id != defaults.dpdk_port_id)
            base.dpdk_port_id = overrides.dpdk_port_id;
        if (overrides.dpdk_socket_mem_mb != defaults.dpdk_socket_mem_mb)
            base.dpdk_socket_mem_mb = overrides.dpdk_socket_mem_mb;
        if (overrides.dpdk_mempool_size != defaults.dpdk_mempool_size)
            base.dpdk_mempool_size = overrides.dpdk_mempool_size;
        if (overrides.dpdk_mbuf_cache_size != defaults.dpdk_mbuf_cache_size)
            base.dpdk_mbuf_cache_size = overrides.dpdk_mbuf_cache_size;
        if (overrides.dpdk_rx_desc != defaults.dpdk_rx_desc)
            base.dpdk_rx_desc = overrides.dpdk_rx_desc;
        if (overrides.dpdk_tx_desc != defaults.dpdk_tx_desc)
            base.dpdk_tx_desc = overrides.dpdk_tx_desc;
        if (overrides.protocol_udp_packet_size != defaults.protocol_udp_packet_size)
        {
            base.protocol_udp_packet_size = overrides.protocol_udp_packet_size;
        }
        if (overrides.protocol_channels_per_prt != defaults.protocol_channels_per_prt)
        {
            base.protocol_channels_per_prt = overrides.protocol_channels_per_prt;
        }
        if (overrides.protocol_packets_per_channel != defaults.protocol_packets_per_channel)
        {
            base.protocol_packets_per_channel = overrides.protocol_packets_per_channel;
        }
        if (overrides.protocol_expected_n_prt != defaults.protocol_expected_n_prt)
        {
            base.protocol_expected_n_prt = overrides.protocol_expected_n_prt;
        }
        if (overrides.protocol_cpi_timeout_ns != defaults.protocol_cpi_timeout_ns)
        {
            base.protocol_cpi_timeout_ns = overrides.protocol_cpi_timeout_ns;
        }
        if (overrides.protocol_dynamic_prt_enabled != defaults.protocol_dynamic_prt_enabled)
        {
            base.protocol_dynamic_prt_enabled = overrides.protocol_dynamic_prt_enabled;
        }
        if (overrides.protocol_max_n_prt != defaults.protocol_max_n_prt)
        {
            base.protocol_max_n_prt = overrides.protocol_max_n_prt;
        }
        if (overrides.raw_record_ring_slots != defaults.raw_record_ring_slots)
            base.raw_record_ring_slots = overrides.raw_record_ring_slots;
        if (overrides.raw_record_writer_batch_size != defaults.raw_record_writer_batch_size)
            base.raw_record_writer_batch_size = overrides.raw_record_writer_batch_size;
        if (overrides.raw_record_max_frame_bytes != defaults.raw_record_max_frame_bytes)
            base.raw_record_max_frame_bytes = overrides.raw_record_max_frame_bytes;
        if (overrides.raw_record_segment_bytes != defaults.raw_record_segment_bytes)
            base.raw_record_segment_bytes = overrides.raw_record_segment_bytes;
        if (overrides.raw_record_max_total_bytes != defaults.raw_record_max_total_bytes)
            base.raw_record_max_total_bytes = overrides.raw_record_max_total_bytes;
        if (overrides.run_until_stopped != defaults.run_until_stopped)
            base.run_until_stopped = overrides.run_until_stopped;
        if (!overrides.cpu_cores.empty())
            base.cpu_cores = overrides.cpu_cores;
        if (overrides.output_drop_policy != defaults.output_drop_policy)
            base.output_drop_policy = overrides.output_drop_policy;
        if (overrides.output_ring_capacity != defaults.output_ring_capacity)
            base.output_ring_capacity = overrides.output_ring_capacity;
        if (overrides.recycle_ring_capacity != defaults.recycle_ring_capacity)
            base.recycle_ring_capacity = overrides.recycle_ring_capacity;
    }

    std::string effective_socket_bind_ip(const RxConfig &config)
    {
        return config.socket_bind_ip.empty() ? config.receiver_ipv4 : config.socket_bind_ip;
    }

    std::uint16_t effective_socket_bind_port(const RxConfig &config)
    {
        const std::uint32_t port = config.socket_bind_port != 0U ? config.socket_bind_port : config.allowed_dest_port;
        if (port == 0U || port > 65535U)
        {
            return 0U;
        }
        return static_cast<std::uint16_t>(port);
    }

    ProtocolSpec protocol_spec_from_config(const RxConfig &config)
    {
        ProtocolSpec spec;
        spec.udp_packet_size = config.protocol_udp_packet_size;
        spec.channels_per_prt = config.protocol_channels_per_prt > 0U && config.protocol_channels_per_prt <= 4U
                                    ? config.protocol_channels_per_prt
                                    : 3U;
        spec.packets_per_channel = config.protocol_packets_per_channel > 0U && config.protocol_packets_per_channel <= 9U
                                       ? config.protocol_packets_per_channel
                                       : 9U;
        spec.packet_data_size =
            spec.udp_packet_size > spec.packet_header_size ? (spec.udp_packet_size - spec.packet_header_size) : 0U;
        spec.control_table_size = spec.udp_packet_size;
        spec.expected_n_prt = config.protocol_expected_n_prt;
        spec.protocol_cpi_timeout_ns = config.protocol_cpi_timeout_ns;
        spec.dynamic_prt_enabled = config.protocol_dynamic_prt_enabled;
        spec.max_n_prt = config.protocol_max_n_prt;
        return spec;
    }

} // namespace rxtech
