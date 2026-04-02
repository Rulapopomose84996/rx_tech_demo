#pragma once

#include <cstddef>
#include <deque>

namespace rxtech {

template <typename T>
class SpscRing {
public:
    explicit SpscRing(std::size_t capacity) : capacity_(capacity) {}

    bool push(const T& value) {
        if (queue_.size() >= capacity_) {
            return false;
        }
        queue_.push_back(value);
        return true;
    }

    bool pop(T& value) {
        if (queue_.empty()) {
            return false;
        }
        value = queue_.front();
        queue_.pop_front();
        return true;
    }

    std::size_t size() const {
        return queue_.size();
    }

private:
    std::size_t capacity_ = 0;
    std::deque<T> queue_;
};

}  // namespace rxtech
