#pragma once
#include <deque>
#include <functional>
#include "vision/VisionResult.h"

namespace edgefocus {

class SlidingWindow {
public:
    SlidingWindow(int64_t window_duration_ms) : window_ms_(window_duration_ms) {}
    
    void add(const VisionResult& res);
    void cleanOld(int64_t current_ms);

    size_t size() const { return window_.size(); }
    const std::deque<VisionResult>& getItems() const { return window_; }

    float getRatioCondition(std::function<bool(const VisionResult&)> condition) const;

private:
    int64_t window_ms_;
    std::deque<VisionResult> window_;
};

} // namespace edgefocus
