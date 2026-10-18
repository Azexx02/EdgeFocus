#include "behavior/SlidingWindow.h"

namespace edgefocus {

void SlidingWindow::add(const VisionResult& res) {
    window_.push_back(res);
    cleanOld(res.timestamp_ms);
}

void SlidingWindow::cleanOld(int64_t current_ms) {
    while (!window_.empty() && (current_ms - window_.front().timestamp_ms > window_ms_)) {
        window_.pop_front();
    }
}

float SlidingWindow::getRatioCondition(std::function<bool(const VisionResult&)> condition) const {
    if (window_.empty()) return 0.0f;
    int count = 0;
    for (const auto& w : window_) {
        if (condition(w)) count++;
    }
    return static_cast<float>(count) / window_.size();
}

} // namespace edgefocus
