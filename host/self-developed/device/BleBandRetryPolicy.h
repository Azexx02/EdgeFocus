#pragma once

#include <algorithm>
#include <chrono>

namespace edgefocus {

class BleBandRetryPolicy {
public:
    std::chrono::seconds nextFailureDelay() {
        const int delay_seconds = std::min(30, 10 << std::min(failure_count_, 2));
        ++failure_count_;
        return std::chrono::seconds(delay_seconds);
    }

    void reset() { failure_count_ = 0; }
    int failureCount() const { return failure_count_; }

private:
    int failure_count_{0};
};

} // namespace edgefocus
