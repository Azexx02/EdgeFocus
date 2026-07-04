#pragma once
#include <string>
#include "behavior/FocusState.h"
#include "behavior/SlidingWindow.h"
#include "config/BehaviorConfig.h"

namespace edgefocus {

enum class PhoneUsageState : uint8_t {
    IN_USE = 0,
    LEARNING = 1,
    PLAYING = 2
};

struct PhoneUsageSignal {
    int64_t timestamp_ms = 0;
    bool active = false;
    float confidence = 1.0f;
    int distraction_level = 1; // phone_sync phone-side code: 0=learning, 1=using, 2=playing, 3=unclassified/high using.
    PhoneUsageState usage_state = PhoneUsageState::IN_USE;
};

struct FocusEvent {
    bool is_looking_away = false;
    bool is_sleepy_or_slumped = false;
    bool is_phone_in_use = false;
    bool is_away = false;
    bool is_low_confidence = false;
    bool vision_signal = false;
    bool phone_signal = false;
    bool visual_phone_detected = false;
    float visual_phone_confidence = 0.0f;
    bool hand_holding_phone = false;
    int phone_distraction_level = -1; // Behavior-layer copy of the phone-side code, not CommandLevel.
    PhoneUsageState phone_usage_state = PhoneUsageState::IN_USE;
    int phone_usage_state_raw = -1;
    int risk_score = 0;
    std::string reason;
};

class EventDetector {
public:
    FocusEvent detect(const SlidingWindow& win, const BehaviorConfig& config);
    
    void injectPhoneSignal(const PhoneUsageSignal& sig);
    void resetPhoneSignal();

private:
    PhoneUsageSignal last_phone_sig_;
};

} // namespace edgefocus
