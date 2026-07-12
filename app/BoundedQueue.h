#pragma once
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <optional>

namespace edgefocus {

/// Thread-safe bounded queue.
/// When full, push() drops the oldest element (for FrameQueue behavior)
/// or rejects the push (for other queues). Controlled by drop_oldest.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity, bool drop_oldest = false)
        : capacity_(capacity), drop_oldest_(drop_oldest) {}

    /// Push an item. If full and drop_oldest, evicts front. Otherwise returns false.
    bool push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) {
            if (drop_oldest_) {
                queue_.pop_front();
            } else {
                return false;
            }
        }
        queue_.push_back(std::move(item));
        cv_.notify_one();
        return true;
    }

    /// Try to pop the front item. Returns nullopt if empty.
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    /// Pop the front item, blocking up to timeout_ms. Returns nullopt on timeout.
    std::optional<T> popFor(int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [this]() { return !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    /// Return only the most recent item, discarding older ones.
    std::optional<T> popLatest() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.back());
        queue_.clear();
        return item;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t capacity() const { return capacity_; }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

private:
    size_t capacity_;
    bool drop_oldest_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
};

} // namespace edgefocus
