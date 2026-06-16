#pragma once
#include "behavior/FocusState.h"
#include "vision/VisionResult.h"
#include "behavior/SlidingWindow.h"
#include "behavior/EventDetector.h"
#include "behavior/RiskScorer.h"
#include "config/BehaviorConfig.h"

namespace edgefocus {

class FocusStateMachine {
public:
    FocusStateMachine();

    void processFrame(const VisionResult& frame);
    void reset();
    FocusState getCurrentState() const { return current_state_; }
    int getRiskScore() const { return last_risk_score_; }
    
    void injectPhoneSignal(const PhoneUsageSignal& sig) {
        detector_.injectPhoneSignal(sig);
    }
    
    BehaviorConfig& getConfig() { return config_; }

private:
    SlidingWindow win30s_; // We only need one window if it covers max history
    EventDetector detector_;
    RiskScorer scorer_;
    BehaviorConfig config_;

    FocusState current_state_;
    FocusState candidate_state_;
    int64_t candidate_start_ms_;
    int last_risk_score_ = 0;

    FocusState determineCandidateState(const FocusEvent& event);
};

} // namespace edgefocus
