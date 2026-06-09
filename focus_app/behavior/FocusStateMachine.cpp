#include "behavior/FocusStateMachine.h"

namespace edgefocus {

FocusStateMachine::FocusStateMachine()
    : win30s_(30000), 
      current_state_(FocusState::CALIBRATING),
      candidate_state_(FocusState::CALIBRATING),
      candidate_start_ms_(0) {}

FocusState FocusStateMachine::determineCandidateState(const FocusEvent& event) {
    if (event.is_low_confidence) return FocusState::LOW_CONFIDENCE;
    if (event.is_away) return FocusState::AWAY;
    if (event.is_phone_in_use) return FocusState::PHONE_IN_USE;
    if (event.is_sleepy_or_slumped) return FocusState::SLEEPY_OR_SLUMPED;
    if (event.is_looking_away) return FocusState::LOOKING_AWAY;
    
    return FocusState::FOCUSED;
}

void FocusStateMachine::processFrame(const VisionResult& frame) {
    win30s_.add(frame);

    // Initial calibration period: require 1 second of data to transition out of CALIBRATING
    if (current_state_ == FocusState::CALIBRATING) {
        if (!win30s_.getItems().empty() && 
            (frame.timestamp_ms - win30s_.getItems().front().timestamp_ms) >= 1000) {
            current_state_ = FocusState::FOCUSED;
        }
        return;
    }

    FocusEvent event = detector_.detect(win30s_, config_);

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
}

void FocusStateMachine::reset() {
    current_state_ = FocusState::CALIBRATING;
    candidate_state_ = FocusState::CALIBRATING;
    candidate_start_ms_ = 0;
    win30s_ = SlidingWindow(30000);
}

} // namespace edgefocus
