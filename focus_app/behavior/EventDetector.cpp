#include "behavior/EventDetector.h"

namespace edgefocus {

void EventDetector::injectPhoneSignal(const PhoneUsageSignal& sig) {
    last_phone_sig_ = sig;
}

static int64_t getContinuousDuration(const SlidingWindow& win, std::function<bool(const VisionResult&)> cond) {
    auto items = win.getItems();
    if (items.empty()) return 0;
    
    int64_t newest_ts = items.back().timestamp_ms;
    int64_t oldest_match_ts = newest_ts;
    
    // Iterate from newest to oldest
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (!cond(*it)) break;
        oldest_match_ts = it->timestamp_ms;
    }
    
    if (oldest_match_ts == newest_ts && !cond(items.back())) return 0;
    return newest_ts - oldest_match_ts;
}

FocusEvent EventDetector::detect(const SlidingWindow& win, const BehaviorConfig& config) {
    FocusEvent event;
    
    auto items = win.getItems();
    if (items.empty()) return event;
    int64_t current_ts = items.back().timestamp_ms;
    
    // Low confidence: ratio > low_confidence_ratio
    event.is_low_confidence = win.getRatioCondition([](const VisionResult& r) { 
        return r.isLowConfidence(); 
    }) > config.low_confidence_ratio;

    if (event.is_low_confidence) {
        // Do not output other interruption events if confidence is low
        return event;
    }

    // Away: continuous duration > away_confirm_ms
    int64_t away_dur = getContinuousDuration(win, [](const VisionResult& r) {
        return r.isAway();
    });
    if (away_dur >= config.away_confirm_ms) {
        event.is_away = true;
    }

    // Sleepy or slumped: Head DOWN (10s) or Eyes CLOSED (legacy fallback)
    int64_t head_down_dur = getContinuousDuration(win, [](const VisionResult& r) {
        return r.isHeadDown();
    });
    int64_t sleepy_dur = getContinuousDuration(win, [](const VisionResult& r) {
        if (r.isEyesClosed()) return true;
        if (r.posture.valid && r.posture.value == PostureType::SLUMPED) return true;
        return false;
    });
    
    if (head_down_dur >= config.slumped_confirm_ms) {
        event.is_sleepy_or_slumped = true;
    } else if (sleepy_dur >= config.sleepy_confirm_ms) {
        event.is_sleepy_or_slumped = true; 
    }

    // Phone in use: Use injected signal
    if (last_phone_sig_.active) {
        if (current_ts - last_phone_sig_.timestamp_ms < 5000) { // Signal is fresh
            // Check if duration exceeds phone_confirm_ms
            // Since we don't have a history of signals, we just trigger if it's active.
            // A more complex implementation would track signal history.
            // For now, if active and fresh, trigger immediately or after simple confirm.
            event.is_phone_in_use = true;
        }
    }

    return event;
}

} // namespace edgefocus
