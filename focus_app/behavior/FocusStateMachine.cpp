#include "behavior/FocusStateMachine.h"

namespace edgefocus {

FocusStateMachine::FocusStateMachine()
    : win30s_(30000), 
      current_state_(FocusState::CALIBRATING),
      candidate_state_(FocusState::CALIBRATING),
      candidate_start_ms_(0) {
    last_event_ = {};
}

FocusState FocusStateMachine::determineCandidateState(const FocusEvent& event) {
    if (event.is_phone_in_use) {
        if (event.phone_usage_state == PhoneUsageState::LEARNING) return FocusState::PHONE_IN_LEARNING;
        if (event.phone_usage_state == PhoneUsageState::PLAYING) return FocusState::PHONE_IN_PLAYING;
        return FocusState::PHONE_IN_USE;
    }
    if (event.is_low_confidence) return FocusState::LOW_CONFIDENCE;
    if (event.is_away) return FocusState::AWAY;
    if (event.is_sleepy_or_slumped) return FocusState::SLEEPY_OR_SLUMPED;
    if (event.is_looking_away) return FocusState::LOOKING_AWAY;
    
    return FocusState::FOCUSED;
}

void FocusStateMachine::processFrame(const VisionResult& frame) {
    win30s_.add(frame);

    // Initial calibration period: require 1 second of data with quality gate
    if (current_state_ == FocusState::CALIBRATING) {
        last_event_ = {};
        last_risk_score_ = 0;
        if (!win30s_.getItems().empty() && 
            (frame.timestamp_ms - win30s_.getItems().front().timestamp_ms) >= 1000) {
            // Require at least some frames with good quality before transitioning
            float good_ratio = win30s_.getRatioCondition([](const VisionResult& r) {
                return r.frame_quality.valid && r.frame_quality.level == FrameQualityLevel::GOOD;
            });
            if (good_ratio >= 0.3f) {
                current_state_ = FocusState::FOCUSED;
            }
        }
        return;
    }

    FocusEvent event = detector_.detect(win30s_, config_);
    last_event_ = event;

    FocusState new_candidate = determineCandidateState(event);

    if (new_candidate != candidate_state_) {
        candidate_state_ = new_candidate;
        candidate_start_ms_ = frame.timestamp_ms;
    }

    // Evaluate hysteresis for state transitions
    if (candidate_state_ != current_state_) {
        int64_t duration = frame.timestamp_ms - candidate_start_ms_;
        
        int64_t required_duration = 0; // Immediate transition for interruptions by default
        
        // Slower transition back to FOCUSED to avoid oscillation
        if (candidate_state_ == FocusState::FOCUSED) {
            required_duration = config_.state_hysteresis_ms;
        }

        if (duration >= required_duration) {
            current_state_ = candidate_state_;
        }
    }

    // Update risk score
    last_risk_score_ = scorer_.calculateScore(event);
    last_event_.risk_score = last_risk_score_;
}

void FocusStateMachine::reset() {
    current_state_ = FocusState::CALIBRATING;
    candidate_state_ = FocusState::CALIBRATING;
    candidate_start_ms_ = 0;
    last_risk_score_ = 0;
    last_event_ = {};
    detector_.resetPhoneSignal();
    win30s_ = SlidingWindow(30000);
}

} // namespace edgefocus
