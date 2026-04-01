#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace rxtech {

class RecentClosedRing {
public:
    explicit RecentClosedRing(std::size_t capacity = 8U) : capacity_(capacity) {
    }

    void push(std::uint64_t cpi_id) {
        if (capacity_ == 0U) {
            return;
        }
        if (entries_.size() == capacity_) {
            entries_.pop_front();
        }
        entries_.push_back(cpi_id);
    }

    bool contains(std::uint64_t cpi_id) const {
        for (std::uint64_t entry : entries_) {
            if (entry == cpi_id) {
                return true;
            }
        }
        return false;
    }

private:
    std::size_t capacity_ = 0;
    std::deque<std::uint64_t> entries_;
};

}  // namespace rxtech
