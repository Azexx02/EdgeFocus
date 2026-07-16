#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace edgefocus {

// Pure state machine for BlueZ advertisement registration. The caller owns
// synchronization and performs the actual D-Bus calls.
class BleAdvertisementRecovery {
public:
    std::optional<std::uint64_t> beginAttempt(std::int64_t now_ms) {
        if (shutting_down_ || registering_ || registered_ || now_ms < next_retry_ms_) {
            return std::nullopt;
        }
        registering_ = true;
        return ++generation_;
    }

    bool complete(std::uint64_t generation, bool success, std::int64_t now_ms) {
        if (shutting_down_ || !registering_ || generation != generation_) return false;
        registering_ = false;
        registered_ = success;
        if (success) {
            failure_count_ = 0;
            next_retry_ms_ = 0;
        } else {
            const auto index = failure_count_ < kRetryDelaysMs.size()
                ? failure_count_
                : kRetryDelaysMs.size() - 1;
            next_retry_ms_ = now_ms + kRetryDelaysMs[index];
            ++failure_count_;
        }
        return true;
    }

    void release(std::int64_t now_ms) {
        ++generation_;
        registering_ = false;
        registered_ = false;
        next_retry_ms_ = now_ms;
    }

    void shutdown() {
        ++generation_;
        shutting_down_ = true;
        registering_ = false;
        registered_ = false;
    }

    void reset() {
        ++generation_;
        shutting_down_ = false;
        registering_ = false;
        registered_ = false;
        failure_count_ = 0;
        next_retry_ms_ = 0;
    }

    bool registering() const { return registering_; }
    bool registered() const { return registered_; }
    bool shuttingDown() const { return shutting_down_; }
    std::int64_t nextRetryMs() const { return next_retry_ms_; }

private:
    inline static constexpr std::array<std::int64_t, 3> kRetryDelaysMs{
        2'000, 5'000, 10'000
    };

    bool shutting_down_ = false;
    bool registering_ = false;
    bool registered_ = false;
    std::uint64_t generation_ = 0;
    std::size_t failure_count_ = 0;
    std::int64_t next_retry_ms_ = 0;
};

} // namespace edgefocus
