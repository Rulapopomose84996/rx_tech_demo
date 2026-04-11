#include "rxtech/raw_frame_recorder.h"

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"
#include "internal/path_utils.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <cstring>
#include <deque>
#include <cerrno>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#define RXTECH_HAS_POSIX_RAW_RECORDER 1
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#define RXTECH_HAS_POSIX_RAW_RECORDER 0
#endif

namespace rxtech
{

    namespace
    {

        struct RawFrameFileHeader
        {
            char magic[8] = {'R', 'X', 'R', 'A', 'W', '0', '1', '\0'};
            std::uint32_t version = 1;
            std::uint32_t header_size = sizeof(RawFrameFileHeader);
            std::uint64_t created_unix_ns = 0;
            std::uint32_t max_frame_bytes = 0;
            std::uint32_t reserved = 0;
        };

        struct RawFrameRecordHeader
        {
            std::uint64_t timestamp_ns = 0;
            std::uint32_t frame_length = 0;
            std::uint32_t queue_id = 0;
        };

        std::uint64_t system_clock_now_ns()
        {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  std::chrono::system_clock::now().time_since_epoch())
                                                  .count());
        }

        std::string make_segment_name(const std::string &prefix, std::uint64_t unix_ns, std::uint64_t counter)
        {
            const std::time_t now = static_cast<std::time_t>(unix_ns / 1'000'000'000ULL);
            std::tm local_time{};
#ifdef _WIN32
            localtime_s(&local_time, &now);
#else
            localtime_r(&now, &local_time);
#endif
            char buffer[32] = {};
            std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local_time);

            std::ostringstream out;
            out << prefix << '_' << buffer << '_' << std::setw(6) << std::setfill('0') << counter << ".rawbin";
            return out.str();
        }

        std::string path_extension(const std::string &path)
        {
            const std::string filename = path_utils::path_filename(path);
            const std::size_t pos = filename.find_last_of('.');
            return pos == std::string::npos ? std::string{} : filename.substr(pos);
        }

        bool path_is_regular_file(const std::string &path)
        {
            struct stat st{};
            return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
        }

        std::uint64_t path_file_size(const std::string &path)
        {
            struct stat st{};
            if (::stat(path.c_str(), &st) != 0)
            {
                throw std::runtime_error("获取文件状态失败: " + path);
            }
            return static_cast<std::uint64_t>(st.st_size);
        }

        void create_directories_if_needed(const std::string &path)
        {
            if (path.empty())
            {
                return;
            }

            std::size_t start = 0U;
            std::string current;
            if (path_utils::is_path_separator(path[0]))
            {
                current = "/";
                start = 1U;
            }

            while (start < path.size())
            {
                while (start < path.size() && path_utils::is_path_separator(path[start]))
                {
                    ++start;
                }
                const std::size_t next = path.find_first_of("/\\", start);
                const std::string part =
                    path.substr(start, next == std::string::npos ? std::string::npos : next - start);
                if (!part.empty())
                {
                    if (!current.empty() && !path_utils::is_path_separator(current.back()))
                    {
                        current.push_back('/');
                    }
                    current += part;
                    if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
                    {
                        throw std::runtime_error("创建目录失败: " + current);
                    }
                }
                if (next == std::string::npos)
                {
                    break;
                }
                start = next + 1U;
            }
        }

        bool remove_file_if_exists(const std::string &path)
        {
            if (::unlink(path.c_str()) == 0)
            {
                return true;
            }
            return errno == ENOENT;
        }

        bool is_managed_segment_file(const std::string &path, const std::string &prefix)
        {
            if (!path_is_regular_file(path) || path_extension(path) != ".rawbin")
            {
                return false;
            }
            const std::string filename = path_utils::path_filename(path);
            const std::string expected_prefix = prefix + "_";
            return filename.rfind(expected_prefix, 0U) == 0U;
        }

    } // namespace

    struct RawFrameRecorder::Impl
    {
        struct Buffer
        {
            std::vector<std::uint8_t> bytes;
            std::uint32_t length = 0;
            std::uint64_t timestamp_ns = 0;
            std::uint32_t queue_id = 0;
        };

        explicit Impl(const RxConfig &config)
            : enabled(config.capture.raw_record_enabled), output_dir(config.capture.raw_record_output_dir),
              file_prefix(config.capture.raw_record_file_prefix.empty() ? "radar_raw"
                                                                        : config.capture.raw_record_file_prefix),
              ring_slots(std::max<std::uint32_t>(1U, config.capture.raw_record_ring_slots)),
              writer_batch_size(std::max<std::uint32_t>(1U, config.capture.raw_record_writer_batch_size)),
              max_frame_bytes(std::max<std::uint32_t>(64U, config.capture.raw_record_max_frame_bytes)),
              max_total_bytes(std::max<std::uint64_t>(1ULL, config.capture.raw_record_max_total_bytes)),
              segment_bytes(
                  std::min(std::max<std::uint64_t>(1ULL, config.capture.raw_record_segment_bytes), max_total_bytes))
        {
        }

        void start()
        {
            if (!enabled || started)
            {
                return;
            }
            if (output_dir.empty())
            {
                throw std::runtime_error("启用原始帧录制时 raw_record_output_dir 不能为空");
            }

            create_directories_if_needed(output_dir);
            buffers.reserve(ring_slots);
            for (std::uint32_t index = 0; index < ring_slots; ++index)
            {
                auto buffer = std::make_unique<Buffer>();
                buffer->bytes.resize(max_frame_bytes);
                free_buffers.push_back(buffer.get());
                buffers.push_back(std::move(buffer));
            }

            {
                std::lock_guard<std::mutex> lock(state_mutex);
                load_existing_segments_locked();
                trim_retention_locked();
                stats.retained_bytes = retained_bytes;
            }

            started = true;
            writer_thread = std::thread([this]() { writer_loop(); });
        }

        void stop()
        {
            if (!enabled || !started)
            {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                stop_requested = true;
            }
            queue_cv.notify_all();
            if (writer_thread.joinable())
            {
                writer_thread.join();
            }
            started = false;
        }

        void submit(const PacketDesc &packet)
        {
            if (!enabled || packet.data == nullptr || packet.len == 0U)
            {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (!failure_message.empty())
                {
                    stats.dropped_frames += 1U;
                    stats.dropped_bytes += packet.len;
                    return;
                }
            }

            if (packet.len > max_frame_bytes)
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                stats.dropped_frames += 1U;
                stats.dropped_bytes += packet.len;
                return;
            }

            Buffer *buffer = nullptr;
            std::size_t queue_depth = 0U;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (stop_requested || free_buffers.empty())
                {
                    buffer = nullptr;
                }
                else
                {
                    buffer = free_buffers.front();
                    free_buffers.pop_front();
                    std::memcpy(buffer->bytes.data(), packet.data, packet.len);
                    buffer->length = packet.len;
                    buffer->timestamp_ns = packet.ts_ns != 0U ? packet.ts_ns : steady_clock_now_ns();
                    buffer->queue_id = packet.queue_id;
                    filled_buffers.push_back(buffer);
                    queue_depth = filled_buffers.size();
                }
            }

            if (buffer == nullptr)
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                stats.dropped_frames += 1U;
                stats.dropped_bytes += packet.len;
                return;
            }

            {
                std::lock_guard<std::mutex> lock(state_mutex);
                stats.queue_high_watermark = std::max<std::uint64_t>(stats.queue_high_watermark, queue_depth);
            }
            queue_cv.notify_one();
        }

        RawFrameRecorderStats snapshot() const
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            return stats;
        }

        std::string error_message_copy() const
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            return failure_message;
        }

        void writer_loop()
        {
            std::vector<Buffer *> batch;
            batch.reserve(writer_batch_size);

            try
            {
                while (true)
                {
                    batch.clear();
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        queue_cv.wait(lock, [this]() { return stop_requested || !filled_buffers.empty(); });
                        if (filled_buffers.empty() && stop_requested)
                        {
                            break;
                        }

                        const std::size_t batch_count = std::min<std::size_t>(writer_batch_size, filled_buffers.size());
                        for (std::size_t index = 0; index < batch_count; ++index)
                        {
                            batch.push_back(filled_buffers.front());
                            filled_buffers.pop_front();
                        }
                    }

                    for (Buffer *buffer : batch)
                    {
                        write_buffer(*buffer);
                    }

                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        for (Buffer *buffer : batch)
                        {
                            buffer->length = 0;
                            free_buffers.push_back(buffer);
                        }
                    }
                }

                close_active_segment();
                {
                    std::lock_guard<std::mutex> lock(state_mutex);
                    trim_retention_locked();
                    stats.retained_bytes = retained_bytes;
                }
            }
            catch (const std::exception &ex)
            {
                {
                    std::lock_guard<std::mutex> lock(state_mutex);
                    failure_message = ex.what();
                }
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    for (Buffer *buffer : batch)
                    {
                        buffer->length = 0;
                        free_buffers.push_back(buffer);
                    }
                    while (!filled_buffers.empty())
                    {
                        Buffer *buffer = filled_buffers.front();
                        filled_buffers.pop_front();
                        buffer->length = 0;
                        free_buffers.push_back(buffer);
                    }
                }
                close_active_segment();
            }
        }

        void write_buffer(const Buffer &buffer)
        {
            const std::uint64_t record_bytes = sizeof(RawFrameRecordHeader) + buffer.length;
            ensure_active_segment(record_bytes);

            RawFrameRecordHeader header;
            header.timestamp_ns = buffer.timestamp_ns;
            header.frame_length = buffer.length;
            header.queue_id = buffer.queue_id;

            active_stream.write(reinterpret_cast<const char *>(&header), static_cast<std::streamsize>(sizeof(header)));
            active_stream.write(reinterpret_cast<const char *>(buffer.bytes.data()),
                                static_cast<std::streamsize>(buffer.length));
            if (!active_stream.good())
            {
                throw std::runtime_error("写入原始帧分段文件失败: " + active_segment_path);
            }

            {
                std::lock_guard<std::mutex> lock(state_mutex);
                active_segment_bytes += record_bytes;
                retained_bytes += record_bytes;
                stats.written_frames += 1U;
                stats.written_bytes += buffer.length;
                stats.retained_bytes = retained_bytes;
                stats.latest_file_path = active_segment_path;
                trim_retention_locked();
                stats.retained_bytes = retained_bytes;
            }
        }

        void ensure_active_segment(std::uint64_t next_record_bytes)
        {
            if (active_stream.is_open() && active_segment_bytes + next_record_bytes <= segment_bytes)
            {
                return;
            }

            close_active_segment();

            const std::string segment_path = path_utils::join_path(
                output_dir, make_segment_name(file_prefix, system_clock_now_ns(), ++segment_counter));
            std::ofstream stream(segment_path, std::ios::binary | std::ios::trunc);
            if (!stream.is_open())
            {
                throw std::runtime_error("打开原始帧分段文件失败: " + segment_path);
            }

            RawFrameFileHeader file_header;
            file_header.created_unix_ns = system_clock_now_ns();
            file_header.max_frame_bytes = max_frame_bytes;
            stream.write(reinterpret_cast<const char *>(&file_header),
                         static_cast<std::streamsize>(sizeof(file_header)));
            if (!stream.good())
            {
                throw std::runtime_error("初始化原始帧分段文件失败: " + segment_path);
            }

            active_stream = std::move(stream);
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                active_segment_path = segment_path;
                active_segment_bytes = sizeof(file_header);
                retained_bytes += sizeof(file_header);
                stats.retained_bytes = retained_bytes;
                stats.latest_file_path = segment_path;
                trim_retention_locked();
                stats.retained_bytes = retained_bytes;
            }
        }

        void close_active_segment()
        {
            if (!active_stream.is_open())
            {
                return;
            }

            active_stream.flush();
            active_stream.close();

            std::lock_guard<std::mutex> lock(state_mutex);
            if (!active_segment_path.empty())
            {
                closed_segments.emplace_back(active_segment_path, active_segment_bytes);
            }
            active_segment_path.clear();
            active_segment_bytes = 0U;
            trim_retention_locked();
            stats.retained_bytes = retained_bytes;
        }

        void load_existing_segments_locked()
        {
            closed_segments.clear();
            retained_bytes = 0U;
            segment_counter = 0U;

#if !RXTECH_HAS_POSIX_RAW_RECORDER
            return;
#else
            DIR *dir = ::opendir(output_dir.c_str());
            if (dir == nullptr)
            {
                return;
            }

            std::vector<std::string> segment_paths;
            while (const dirent *entry = ::readdir(dir))
            {
                const std::string name = entry->d_name;
                if (name == "." || name == "..")
                {
                    continue;
                }
                const std::string path = path_utils::join_path(output_dir, name);
                if (is_managed_segment_file(path, file_prefix))
                {
                    segment_paths.push_back(path);
                }
            }
            ::closedir(dir);

            std::sort(segment_paths.begin(), segment_paths.end());
            for (const std::string &path : segment_paths)
            {
                const std::uint64_t file_size = path_file_size(path);
                retained_bytes += file_size;
                closed_segments.emplace_back(path, file_size);
                ++segment_counter;
            }
#endif
        }

        void trim_retention_locked()
        {
            while (retained_bytes > max_total_bytes && !closed_segments.empty())
            {
                const auto oldest = closed_segments.front();
                closed_segments.pop_front();
                if (remove_file_if_exists(oldest.first))
                {
                    retained_bytes = retained_bytes > oldest.second ? (retained_bytes - oldest.second) : 0U;
                }
            }
        }

        bool enabled = false;
        std::string output_dir;
        std::string file_prefix;
        std::uint32_t ring_slots = 0;
        std::uint32_t writer_batch_size = 0;
        std::uint32_t max_frame_bytes = 0;
        std::uint64_t max_total_bytes = 0;
        std::uint64_t segment_bytes = 0;

        std::vector<std::unique_ptr<Buffer>> buffers;
        std::deque<Buffer *> free_buffers;
        std::deque<Buffer *> filled_buffers;
        std::thread writer_thread;

        mutable std::mutex queue_mutex;
        std::condition_variable queue_cv;
        bool started = false;
        bool stop_requested = false;

        mutable std::mutex state_mutex;
        RawFrameRecorderStats stats;
        std::deque<std::pair<std::string, std::uint64_t>> closed_segments;
        std::string active_segment_path;
        std::ofstream active_stream;
        std::uint64_t active_segment_bytes = 0;
        std::uint64_t retained_bytes = 0;
        std::uint64_t segment_counter = 0;
        std::string failure_message;
    };

    RawFrameRecorder::RawFrameRecorder(const RxConfig &config) : impl_(std::make_unique<Impl>(config)) {}

    RawFrameRecorder::~RawFrameRecorder()
    {
        if (impl_ != nullptr)
        {
            impl_->stop();
        }
    }

    void RawFrameRecorder::start()
    {
        if (impl_ != nullptr)
        {
            impl_->start();
        }
    }

    void RawFrameRecorder::stop()
    {
        if (impl_ != nullptr)
        {
            impl_->stop();
        }
    }

    void RawFrameRecorder::submit(const PacketDesc &packet)
    {
        if (impl_ != nullptr)
        {
            impl_->submit(packet);
        }
    }

    bool RawFrameRecorder::enabled() const
    {
        return impl_ != nullptr && impl_->enabled;
    }

    const std::string &RawFrameRecorder::output_dir() const
    {
        static const std::string empty;
        return impl_ != nullptr ? impl_->output_dir : empty;
    }

    RawFrameRecorderStats RawFrameRecorder::snapshot() const
    {
        if (impl_ == nullptr)
        {
            return {};
        }
        return impl_->snapshot();
    }

    std::string RawFrameRecorder::error_message() const
    {
        if (impl_ == nullptr)
        {
            return {};
        }
        return impl_->error_message_copy();
    }

} // namespace rxtech
