#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace rxtech {

/// Lock-free single-producer / single-consumer ring buffer.
/// Producer calls push(), consumer calls pop().
/// Capacity is rounded up to a power of two internally.
template <typename T>
class SpscRing {
    static_assert(std::is_trivially_copyable<T>::value || std::is_move_constructible<T>::value,
                  "SpscRing element must be trivially copyable or move constructible");

public:
    explicit SpscRing(std::size_t min_capacity)
    {
        // One slot is sacrificed to distinguish full vs empty,
        // so allocate min_capacity + 1, rounded to next power of two.
        std::size_t cap = 2;
        while (cap < min_capacity + 1U) {
            cap <<= 1U;
        }
        capacity_ = cap;
        mask_ = cap - 1U;
        buf_ = std::make_unique<Slot[]>(cap);
    }

    /// Producer-only. Returns false if ring is full.
    bool push(const T& value) noexcept(std::is_nothrow_copy_constructible<T>::value)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1U) & mask_;
        if (next == tail_cached_) {
            tail_cached_ = tail_.load(std::memory_order_acquire);
            if (next == tail_cached_) {
                return false;
            }
        }
        buf_[head].value = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Consumer-only. Returns false if ring is empty.
    bool pop(T& out) noexcept(std::is_nothrow_move_assignable<T>::value)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_cached_) {
            head_cached_ = head_.load(std::memory_order_acquire);
            if (tail == head_cached_) {
                return false;
            }
        }
        out = std::move(buf_[tail].value);
        tail_.store((tail + 1U) & mask_, std::memory_order_release);
        return true;
    }

    /// Approximate size (racy but useful for metrics).
    std::size_t size_approx() const noexcept
    {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & mask_;
    }

    /// Usable capacity (one internal slot is reserved for full/empty distinction).
    std::size_t capacity() const noexcept { return capacity_ - 1U; }

private:
    struct Slot {
        T value{};
    };

    std::unique_ptr<Slot[]> buf_;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;

    // Producer side
    alignas(64) std::atomic<std::size_t> head_{0};
    std::size_t tail_cached_ = 0; // producer-local cache of tail_

    // Consumer side
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::size_t head_cached_ = 0; // consumer-local cache of head_
};

}  // namespace rxtech
