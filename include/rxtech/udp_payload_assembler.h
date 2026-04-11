#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rxtech/packet_desc.h"

namespace rxtech
{

    class UdpPayloadBuffer
    {
      public:
        using value_type = std::uint8_t;
        using const_iterator = const value_type *;

        UdpPayloadBuffer() = default;

        UdpPayloadBuffer(const UdpPayloadBuffer &other)
        {
            *this = other;
        }

        UdpPayloadBuffer(UdpPayloadBuffer &&other) noexcept
        {
            *this = std::move(other);
        }

        UdpPayloadBuffer &operator=(const UdpPayloadBuffer &other)
        {
            if (this == &other)
            {
                return *this;
            }

            if (other.has_owned_payload())
            {
                owned_payload_ = other.owned_payload_;
                sync_view_to_owned();
                return *this;
            }

            assign(other.view_, other.size_);
            return *this;
        }

        UdpPayloadBuffer &operator=(UdpPayloadBuffer &&other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }

            owned_payload_ = std::move(other.owned_payload_);
            size_ = other.size_;
            view_ = owned_payload_.empty() ? other.view_ : owned_data();
            if (size_ == 0U)
            {
                view_ = empty_data();
            }

            other.clear();
            return *this;
        }

        UdpPayloadBuffer &operator=(const std::vector<value_type> &payload)
        {
            assign(payload.data(), payload.size());
            return *this;
        }

        UdpPayloadBuffer &operator=(std::vector<value_type> &&payload) noexcept
        {
            owned_payload_ = std::move(payload);
            sync_view_to_owned();
            return *this;
        }

        void clear() noexcept
        {
            owned_payload_.clear();
            view_ = empty_data();
            size_ = 0U;
        }

        void set_view(const std::uint8_t *data, std::size_t size) noexcept
        {
            owned_payload_.clear();
            view_ = size == 0U ? empty_data() : data;
            size_ = size;
        }

        void assign(const std::uint8_t *data, std::size_t size)
        {
            if (data == nullptr || size == 0U)
            {
                clear();
                return;
            }

            owned_payload_.assign(data, data + size);
            sync_view_to_owned();
        }

        void assign(const std::uint8_t *first, const std::uint8_t *last)
        {
            if (first == nullptr || last == nullptr || first == last)
            {
                clear();
                return;
            }

            owned_payload_.assign(first, last);
            sync_view_to_owned();
        }

        void resize(std::size_t new_size, value_type value = 0U)
        {
            materialize_if_viewed();
            owned_payload_.resize(new_size, value);
            sync_view_to_owned();
        }

        const std::uint8_t *data() const noexcept
        {
            return view_;
        }

        std::size_t size() const noexcept
        {
            return size_;
        }

        bool empty() const noexcept
        {
            return size_ == 0U;
        }

        const_iterator begin() const noexcept
        {
            return data();
        }

        const_iterator end() const noexcept
        {
            return data() + size_;
        }

      private:
        bool has_owned_payload() const noexcept
        {
            return !owned_payload_.empty();
        }

        static const std::uint8_t *empty_data() noexcept
        {
            static const std::uint8_t kEmpty = 0U;
            return &kEmpty;
        }

        const std::uint8_t *owned_data() const noexcept
        {
            return owned_payload_.empty() ? empty_data() : owned_payload_.data();
        }

        void sync_view_to_owned() noexcept
        {
            size_ = owned_payload_.size();
            view_ = owned_data();
        }

        void materialize_if_viewed()
        {
            if (has_owned_payload() || size_ == 0U)
            {
                return;
            }

            owned_payload_.assign(view_, view_ + size_);
            sync_view_to_owned();
        }

        std::vector<std::uint8_t> owned_payload_;
        const std::uint8_t *view_ = empty_data();
        std::size_t size_ = 0U;
    };

    struct UdpPayloadFrame
    {
        UdpPayloadBuffer udp_payload;
        std::uint32_t source_ipv4_be = 0;
        std::uint32_t dest_ipv4_be = 0;
        std::uint16_t source_port = 0;
        std::uint16_t dest_port = 0;
        bool has_global_sequence = false;
        std::uint16_t global_sequence = 0;
    };

    class UdpPayloadFrameBatch
    {
      public:
        using value_type = UdpPayloadFrame;
        using iterator = value_type *;
        using const_iterator = const value_type *;

        void clear() noexcept
        {
            size_ = 0U;
        }

        void push_back(value_type value) noexcept
        {
            if (size_ >= storage_.size())
            {
                return;
            }
            storage_[size_++] = std::move(value);
        }

        std::size_t size() const noexcept
        {
            return size_;
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
        std::array<value_type, 1U> storage_{};
        std::size_t size_ = 0U;
    };

    class UdpPayloadAssembler
    {
      public:
        UdpPayloadFrameBatch push(const PacketDesc &packet);

      private:
        struct FragmentKey
        {
            std::uint32_t source_ipv4_be = 0;
            std::uint32_t dest_ipv4_be = 0;
            std::uint16_t identification = 0;
            std::uint8_t protocol = 0;

            bool operator==(const FragmentKey &other) const;
        };

        struct FragmentKeyHash
        {
            std::size_t operator()(const FragmentKey &key) const noexcept;
        };

        struct FragmentAssembly
        {
            std::map<std::uint16_t, std::vector<std::uint8_t>> pieces;
            std::uint64_t first_seen_ns = 0;
            std::uint16_t total_payload_length = 0;
            bool has_total_length = false;
            std::uint16_t source_port = 0;
            std::uint16_t dest_port = 0;
            std::uint16_t udp_length = 0;
            bool has_udp_header = false;
        };

        std::unordered_map<FragmentKey, FragmentAssembly, FragmentKeyHash> fragments_;
    };

} // namespace rxtech
