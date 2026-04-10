#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rxtech
{

    constexpr std::size_t kUdpDatagramBurstCapacity = 1024U;

    enum class BackendKind : std::uint8_t
    {
        unknown = 0,
        dpdk,
        socket,
        file_replay
    };

    struct UdpDatagramDesc
    {
        const std::uint8_t *payload_data = nullptr;
        std::uint32_t payload_len = 0;
        const std::uint8_t *raw_frame_data = nullptr;
        std::uint32_t raw_frame_len = 0;
        std::uint32_t src_ipv4_be = 0;
        std::uint32_t dst_ipv4_be = 0;
        std::uint16_t src_port = 0;
        std::uint16_t dst_port = 0;
        std::uint64_t ts_ns = 0;
        std::uint32_t queue_id = 0;
        std::uintptr_t cookie = 0;
        BackendKind backend_kind = BackendKind::unknown;
        bool has_global_sequence = false;
        std::uint16_t global_sequence = 0;
        bool truncated = false;
    };

    class UdpDatagramBurstStorage
    {
      public:
        using value_type = UdpDatagramDesc;
        using iterator = value_type *;
        using const_iterator = const value_type *;

        void clear() noexcept
        {
            size_ = 0U;
        }

        void reserve(std::size_t) noexcept {}

        void resize(std::size_t new_size) noexcept
        {
            size_ = new_size <= storage_.size() ? new_size : storage_.size();
        }

        void push_back(const value_type &value) noexcept
        {
            if (size_ >= storage_.size())
            {
                return;
            }
            storage_[size_++] = value;
        }

        std::size_t size() const noexcept
        {
            return size_;
        }

        constexpr std::size_t capacity() const noexcept
        {
            return kUdpDatagramBurstCapacity;
        }

        constexpr std::size_t max_size() const noexcept
        {
            return kUdpDatagramBurstCapacity;
        }

        bool empty() const noexcept
        {
            return size_ == 0U;
        }

        value_type &operator[](std::size_t index) noexcept
        {
            return storage_[index];
        }

        const value_type &operator[](std::size_t index) const noexcept
        {
            return storage_[index];
        }

        iterator begin() noexcept
        {
            return storage_.data();
        }

        iterator end() noexcept
        {
            return storage_.data() + size_;
        }

        const_iterator begin() const noexcept
        {
            return storage_.data();
        }

        const_iterator end() const noexcept
        {
            return storage_.data() + size_;
        }

      private:
        std::array<value_type, kUdpDatagramBurstCapacity> storage_{};
        std::size_t size_ = 0U;
    };

    struct UdpDatagramBurst
    {
        UdpDatagramBurstStorage datagrams;
    };

} // namespace rxtech
