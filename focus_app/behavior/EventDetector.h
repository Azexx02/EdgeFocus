#pragma once
#include "behavior/SlidingWindow.h"
#include "config/BehaviorConfig.h"

namespace edgefocus {

struct PhoneUsageSignal {
    int64_t timestamp_ms = 0;
    bool active = false;
    float confidence = 1.0f;
    int distraction_level = 1; // 0=learning, 1=weak, 2=strong, 3=unclassified
};

struct FocusEvent {
    bool is_looking_away = false;
    bool is_sleepy_or_slumped = false;
    bool is_phone_in_use = false;
    bool is_away = false;
    bool is_low_confidence = false;
    int phone_distraction_level = 1; // 0=learning, 1=weak, 2=strong, 3=unclassified
};

class EventDetector {
public:
    FocusEvent detect(const SlidingWindow& win, const BehaviorConfig& config);
    
    void injectPhoneSignal(const PhoneUsageSignal& sig);

private:
    PhoneUsageSignal last_phone_sig_;
};

} // namespace edgefocus
