#include "rxtech/rx_config.h"

#include "rxtech/protocol_spec.h"

#include <cctype>
#include <fstream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

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

        bool should_skip_assignment(const std::string &canonical_key,
                                    bool section_key,
                                    std::unordered_set<std::string> &section_assigned_keys)
        {
            if (section_key)
            {
                section_assigned_keys.insert(canonical_key);
                return false;
            }
            return section_assigned_keys.count(canonical_key) > 0U;
        }

        void assign_config_value(RxConfig &config,
                                 const std::string &key,
                                 const std::string &value,
                                 bool section_key,
                                 std::unordered_set<std::string> &section_assigned_keys)
        {
            const std::string normalized_key = to_lower(trim(key));
            const std::string normalized_value = strip_quotes(trim(value));

            if (normalized_key == "backend" || normalized_key == "backend_name")
            {
                if (should_skip_assignment("backend_name", section_key, section_assigned_keys))
                {
                    return;
                }
                config.backend_name = normalized_value;
            }
            else if (normalized_key == "output" || normalized_key == "output_dir" ||
                     normalized_key == "capture_output_dir" || normalized_key == "capture.output_dir")
            {
                if (should_skip_assignment("capture_output_dir", section_key, section_assigned_keys))
                {
                    return;
                }
                config.output_dir = normalized_value;
                config.capture_output_dir = normalized_value;
            }
            else if (normalized_key == "capture.enabled" || normalized_key == "capture_enabled")
            {
                if (should_skip_assignment("capture_enabled", section_key, section_assigned_keys))
                {
                    return;
                }
                config.capture_enabled = parse_bool(normalized_value);
            }
            else if (normalized_key == "capture.index_filename" || normalized_key == "capture_index_filename")
            {
                if (should_skip_assignment("capture_index_filename", section_key, section_assigned_keys))
                {
                    return;
                }
                config.capture_index_filename = normalized_value;
            }
            else if (normalized_key == "capture.data_filename" || normalized_key == "capture_data_filename")
            {
                if (should_skip_assignment("capture_data_filename", section_key, section_assigned_keys))
                {
                    return;
                }
                config.capture_data_filename = normalized_value;
            }
            else if (normalized_key == "interface" || normalized_key == "interface_name" ||
                     normalized_key == "network.interface_name")
            {
                if (should_skip_assignment("interface_name", section_key, section_assigned_keys))
                {
                    return;
                }
                config.interface_name = normalized_value;
            }
            else if (normalized_key == "receiver_ipv4" || normalized_key == "network.receiver_ipv4")
            {
                if (should_skip_assignment("receiver_ipv4", section_key, section_assigned_keys))
                {
                    return;
                }
                config.receiver_ipv4 = normalized_value;
            }
            else if (normalized_key == "allowed_source_ipv4" || normalized_key == "network.allowed_source_ipv4")
            {
                if (should_skip_assignment("allowed_source_ipv4", section_key, section_assigned_keys))
                {
                    return;
                }
                config.allowed_source_ipv4 = normalized_value;
            }
            else if (normalized_key == "queue_id" || normalized_key == "network.queue_id")
            {
                if (should_skip_assignment("queue_id", section_key, section_assigned_keys))
                {
                    return;
                }
                config.queue_id = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "max_burst" || normalized_key == "runtime.max_burst")
            {
                if (should_skip_assignment("max_burst", section_key, section_assigned_keys))
                {
                    return;
                }
                config.max_burst = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "xdp_rx_ring_size")
            {
                if (should_skip_assignment("xdp_rx_ring_size", section_key, section_assigned_keys))
                {
                    return;
                }
                config.xdp_rx_ring_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "xdp_tx_ring_size")
            {
                if (should_skip_assignment("xdp_tx_ring_size", section_key, section_assigned_keys))
                {
                    return;
                }
                config.xdp_tx_ring_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "xdp_fill_ring_size")
            {
                if (should_skip_assignment("xdp_fill_ring_size", section_key, section_assigned_keys))
                {
                    return;
                }
                config.xdp_fill_ring_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "xdp_completion_ring_size")
            {
                if (should_skip_assignment("xdp_completion_ring_size", section_key, section_assigned_keys))
                {
                    return;
                }
                config.xdp_completion_ring_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "xdp_frame_size")
            {
                if (should_skip_assignment("xdp_frame_size", section_key, section_assigned_keys))
                {
                    return;
                }
                config.xdp_frame_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "xdp_frame_count")
            {
                if (should_skip_assignment("xdp_frame_count", section_key, section_assigned_keys))
                {
                    return;
                }
                config.xdp_frame_count = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "xdp_poll_timeout_ms")
            {
                if (should_skip_assignment("xdp_poll_timeout_ms", section_key, section_assigned_keys))
                {
                    return;
                }
                config.xdp_poll_timeout_ms = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "duration_seconds" || normalized_key == "runtime.duration_seconds")
            {
                if (should_skip_assignment("duration_seconds", section_key, section_assigned_keys))
                {
                    return;
                }
                config.duration_seconds = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "packet_size_bytes")
            {
                if (should_skip_assignment("packet_size_bytes", section_key, section_assigned_keys))
                {
                    return;
                }
                config.packet_size_bytes = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "status_interval_seconds" || normalized_key == "runtime.status_interval_seconds")
            {
                if (should_skip_assignment("status_interval_seconds", section_key, section_assigned_keys))
                {
                    return;
                }
                config.status_interval_seconds = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "feedback_interval_seconds" || normalized_key == "feedback.interval_seconds")
            {
                if (should_skip_assignment("feedback_interval_seconds", section_key, section_assigned_keys))
                {
                    return;
                }
                config.feedback_interval_seconds = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "feedback_port" || normalized_key == "feedback.port")
            {
                if (should_skip_assignment("feedback_port", section_key, section_assigned_keys))
                {
                    return;
                }
                config.feedback_port = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "allowed_dest_port" || normalized_key == "network.allowed_dest_port")
            {
                if (should_skip_assignment("allowed_dest_port", section_key, section_assigned_keys))
                {
                    return;
                }
                config.allowed_dest_port = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "feedback_host" || normalized_key == "feedback.host")
            {
                if (should_skip_assignment("feedback_host", section_key, section_assigned_keys))
                {
                    return;
                }
                config.feedback_host = normalized_value;
            }
            else if (normalized_key == "feedback_bind_host" || normalized_key == "feedback.bind_host")
            {
                if (should_skip_assignment("feedback_bind_host", section_key, section_assigned_keys))
                {
                    return;
                }
                config.feedback_bind_host = normalized_value;
            }
            else if (normalized_key == "feedback_enabled" || normalized_key == "feedback.enabled")
            {
                if (should_skip_assignment("feedback_enabled", section_key, section_assigned_keys))
                {
                    return;
                }
                config.feedback_enabled = parse_bool(normalized_value);
            }
            else if (normalized_key == "cpu_cores" || normalized_key == "runtime.cpu_cores")
            {
                if (should_skip_assignment("cpu_cores", section_key, section_assigned_keys))
                {
                    return;
                }
                config.cpu_cores = parse_int_list(normalized_value);
            }
            else if (normalized_key == "run_until_stopped" || normalized_key == "runtime.run_until_stopped")
            {
                if (should_skip_assignment("run_until_stopped", section_key, section_assigned_keys))
                {
                    return;
                }
                config.run_until_stopped = parse_bool(normalized_value);
            }
            else if (normalized_key == "xdp_bind_mode")
            {
                if (should_skip_assignment("xdp_bind_mode", section_key, section_assigned_keys))
                {
                    return;
                }
                config.xdp_bind_mode = normalized_value;
            }
            else if (normalized_key == "dpdk_port_id" || normalized_key == "dpdk.port_id")
            {
                if (should_skip_assignment("dpdk_port_id", section_key, section_assigned_keys))
                {
                    return;
                }
                config.dpdk_port_id = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "dpdk_pci_addr" || normalized_key == "dpdk.pci_addr")
            {
                if (should_skip_assignment("dpdk_pci_addr", section_key, section_assigned_keys))
                {
                    return;
                }
                config.dpdk_pci_addr = normalized_value;
            }
            else if (normalized_key == "dpdk_socket_mem_mb" || normalized_key == "dpdk.socket_mem_mb")
            {
                if (should_skip_assignment("dpdk_socket_mem_mb", section_key, section_assigned_keys))
                {
                    return;
                }
                config.dpdk_socket_mem_mb = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "dpdk_mempool_size" || normalized_key == "dpdk.mempool_size")
            {
                if (should_skip_assignment("dpdk_mempool_size", section_key, section_assigned_keys))
                {
                    return;
                }
                config.dpdk_mempool_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "dpdk_mbuf_cache_size" || normalized_key == "dpdk.mbuf_cache_size")
            {
                if (should_skip_assignment("dpdk_mbuf_cache_size", section_key, section_assigned_keys))
                {
                    return;
                }
                config.dpdk_mbuf_cache_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "dpdk_rx_desc" || normalized_key == "dpdk.rx_desc")
            {
                if (should_skip_assignment("dpdk_rx_desc", section_key, section_assigned_keys))
                {
                    return;
                }
                config.dpdk_rx_desc = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "dpdk_tx_desc" || normalized_key == "dpdk.tx_desc")
            {
                if (should_skip_assignment("dpdk_tx_desc", section_key, section_assigned_keys))
                {
                    return;
                }
                config.dpdk_tx_desc = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "log.level" || normalized_key == "log_level")
            {
                if (should_skip_assignment("log_level", section_key, section_assigned_keys))
                {
                    return;
                }
                config.log_level = to_lower(normalized_value);
            }
            else if (normalized_key == "log.output" || normalized_key == "log_output")
            {
                if (should_skip_assignment("log_output", section_key, section_assigned_keys))
                {
                    return;
                }
                config.log_output = to_lower(normalized_value);
            }
            else if (normalized_key == "log.file_path" || normalized_key == "log_file_path")
            {
                if (should_skip_assignment("log_file_path", section_key, section_assigned_keys))
                {
                    return;
                }
                config.log_file_path = normalized_value;
            }
            else if (normalized_key == "protocol.udp_packet_size" || normalized_key == "protocol_udp_packet_size")
            {
                if (should_skip_assignment("protocol_udp_packet_size", section_key, section_assigned_keys))
                {
                    return;
                }
                config.protocol_udp_packet_size = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "protocol.channels_per_prt" || normalized_key == "protocol_channels_per_prt")
            {
                if (should_skip_assignment("protocol_channels_per_prt", section_key, section_assigned_keys))
                {
                    return;
                }
                config.protocol_channels_per_prt = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
            else if (normalized_key == "protocol.packets_per_channel" || normalized_key == "protocol_packets_per_channel")
            {
                if (should_skip_assignment("protocol_packets_per_channel", section_key, section_assigned_keys))
                {
                    return;
                }
                config.protocol_packets_per_channel = static_cast<std::uint32_t>(std::stoul(normalized_value));
            }
        }

    } // namespace

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
            throw std::runtime_error("failed to open config file: " + path);
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
        if (!overrides.interface_name.empty())
            base.interface_name = overrides.interface_name;
        if (!overrides.receiver_ipv4.empty())
            base.receiver_ipv4 = overrides.receiver_ipv4;
        if (!overrides.allowed_source_ipv4.empty())
            base.allowed_source_ipv4 = overrides.allowed_source_ipv4;
        if (!overrides.dpdk_pci_addr.empty())
            base.dpdk_pci_addr = overrides.dpdk_pci_addr;
        if (!overrides.xdp_bind_mode.empty())
            base.xdp_bind_mode = overrides.xdp_bind_mode;
        if (!overrides.feedback_host.empty())
            base.feedback_host = overrides.feedback_host;
        if (!overrides.feedback_bind_host.empty())
            base.feedback_bind_host = overrides.feedback_bind_host;
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
        if (overrides.xdp_rx_ring_size != defaults.xdp_rx_ring_size)
            base.xdp_rx_ring_size = overrides.xdp_rx_ring_size;
        if (overrides.xdp_tx_ring_size != defaults.xdp_tx_ring_size)
            base.xdp_tx_ring_size = overrides.xdp_tx_ring_size;
        if (overrides.xdp_fill_ring_size != defaults.xdp_fill_ring_size)
            base.xdp_fill_ring_size = overrides.xdp_fill_ring_size;
        if (overrides.xdp_completion_ring_size != defaults.xdp_completion_ring_size)
        {
            base.xdp_completion_ring_size = overrides.xdp_completion_ring_size;
        }
        if (overrides.xdp_frame_size != defaults.xdp_frame_size)
            base.xdp_frame_size = overrides.xdp_frame_size;
        if (overrides.xdp_frame_count != defaults.xdp_frame_count)
            base.xdp_frame_count = overrides.xdp_frame_count;
        if (overrides.xdp_poll_timeout_ms != defaults.xdp_poll_timeout_ms)
        {
            base.xdp_poll_timeout_ms = overrides.xdp_poll_timeout_ms;
        }
        if (overrides.duration_seconds != 0U)
            base.duration_seconds = overrides.duration_seconds;
        if (overrides.packet_size_bytes != defaults.packet_size_bytes)
            base.packet_size_bytes = overrides.packet_size_bytes;
        if (overrides.status_interval_seconds != defaults.status_interval_seconds)
            base.status_interval_seconds = overrides.status_interval_seconds;
        if (overrides.feedback_interval_seconds != defaults.feedback_interval_seconds)
            base.feedback_interval_seconds = overrides.feedback_interval_seconds;
        if (overrides.feedback_port != defaults.feedback_port)
            base.feedback_port = overrides.feedback_port;
        if (overrides.allowed_dest_port != defaults.allowed_dest_port)
            base.allowed_dest_port = overrides.allowed_dest_port;
        if (overrides.feedback_enabled != defaults.feedback_enabled)
            base.feedback_enabled = overrides.feedback_enabled;
        if (overrides.capture_enabled != defaults.capture_enabled)
            base.capture_enabled = overrides.capture_enabled;
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
        if (overrides.run_until_stopped != defaults.run_until_stopped)
            base.run_until_stopped = overrides.run_until_stopped;
        if (!overrides.cpu_cores.empty())
            base.cpu_cores = overrides.cpu_cores;
    }

    ProtocolSpec protocol_spec_from_config(const RxConfig &config)
    {
        ProtocolSpec spec;
        spec.udp_packet_size = config.protocol_udp_packet_size;
        spec.channels_per_prt = config.protocol_channels_per_prt;
        spec.packets_per_channel = config.protocol_packets_per_channel;
        spec.packet_data_size =
            spec.udp_packet_size > spec.packet_header_size ? (spec.udp_packet_size - spec.packet_header_size) : 0U;
        spec.control_table_size = spec.udp_packet_size;
        return spec;
    }

} // namespace rxtech
