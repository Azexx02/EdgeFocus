#include "behavior/FocusStateMachine.h"
#include "behavior/RiskScorer.h"
#include "vision/VisionResult.h"
#include <iostream>
#include <cassert>

using namespace edgefocus;

VisionResult createFrame(int64_t ts) {
    VisionResult r;
    r.timestamp_ms = ts;
    r.frame_quality.valid = true;
    r.frame_quality.level = FrameQualityLevel::GOOD;
    r.presence_conf = 0.9f;
    r.head_down_score = 0.0f;
    r.head_state = HeadState::NORMAL;
    r.presence.valid = true;
    r.presence.value = Presence::PRESENT;
    r.presence.confidence = 0.9f;
    return r;
}

void test_focus() {
    FocusStateMachine fsm;
    for (int i = 0; i < 50; ++i) { // 5 seconds at 10fps
        VisionResult r = createFrame(i * 100);
        fsm.processFrame(r);
    }
    assert(fsm.getCurrentState() == FocusState::FOCUSED);
    std::cout << "test_focus passed\n";
}

void test_head_down() {
    FocusStateMachine fsm;
    for (int i = 0; i < 10; ++i) fsm.processFrame(createFrame(i * 100));
    
    // Head down for 5 seconds (grace is 10s now)
    for (int i = 10; i < 60; ++i) {
        VisionResult r = createFrame(i * 100);
        r.head_down_score = 0.8f;
        r.head_state = HeadState::DOWN;
        fsm.processFrame(r);
    }
    assert(fsm.getCurrentState() == FocusState::FOCUSED);
    
    // Continue head down until 11s (110 frames total)
    for (int i = 60; i < 120; ++i) {
        VisionResult r = createFrame(i * 100);
        r.head_down_score = 0.8f;
        r.head_state = HeadState::DOWN;
        fsm.processFrame(r);
    }
    assert(fsm.getCurrentState() == FocusState::SLEEPY_OR_SLUMPED);
    std::cout << "test_head_down passed\n";
}

void test_phone_in_use() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100)); // 2s, calibration done
    
    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 2000;
    fsm.injectPhoneSignal(sig);
    
    // process one frame after injecting
    fsm.processFrame(createFrame(2100));
    
    assert(fsm.getCurrentState() == FocusState::PHONE_IN_USE);
    std::cout << "test_phone_in_use passed\n";
}

void test_away() {
    FocusStateMachine fsm;
    for (int i = 0; i < 10; ++i) fsm.processFrame(createFrame(i * 100));
    
    for (int i = 10; i < 70; ++i) { // 6 seconds
        VisionResult r = createFrame(i * 100);
        r.presence.value = Presence::AWAY;
        r.presence.confidence = 0.9f;
        fsm.processFrame(r);
    }
    assert(fsm.getCurrentState() == FocusState::AWAY);
    std::cout << "test_away passed\n";
}

void test_low_confidence() {
    FocusStateMachine fsm;
    for (int i = 0; i < 10; ++i) fsm.processFrame(createFrame(i * 100));
    
    // Simulate low confidence
    for (int i = 10; i < 40; ++i) {
        VisionResult r = createFrame(i * 100);
        r.reason_code = ReasonCode::LOW_CONFIDENCE;
        fsm.processFrame(r);
    }
    assert(fsm.getCurrentState() == FocusState::LOW_CONFIDENCE);
    std::cout << "test_low_confidence passed\n";
}

void test_phone_signal_survives_low_confidence_vision() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));

    for (int i = 20; i < 70; ++i) {
        VisionResult r = createFrame(i * 100);
        r.reason_code = ReasonCode::LOW_CONFIDENCE;
        fsm.processFrame(r);
    }

    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 7000;
    sig.distraction_level = 2;
    fsm.injectPhoneSignal(sig);

    VisionResult low_conf_phone_frame = createFrame(7100);
    low_conf_phone_frame.reason_code = ReasonCode::LOW_CONFIDENCE;
    fsm.processFrame(low_conf_phone_frame);

    assert(fsm.getCurrentState() == FocusState::LOW_CONFIDENCE);
    assert(fsm.getLastEvent().phone_signal);
    assert(fsm.getLastEvent().is_low_confidence);
    std::cout << "test_phone_signal_survives_low_confidence_vision passed\n";
}

void test_hysteresis() {
    FocusStateMachine fsm;
    for (int i = 0; i < 10; ++i) fsm.processFrame(createFrame(i * 100));
    
    // Away to trigger state
    for (int i = 10; i < 70; ++i) {
        VisionResult r = createFrame(i * 100);
        r.presence.value = Presence::AWAY;
        fsm.processFrame(r);
    }
    assert(fsm.getCurrentState() == FocusState::AWAY);
    
    // Now return to FOCUSED but not long enough
    for (int i = 70; i < 90; ++i) { // 2 seconds
        fsm.processFrame(createFrame(i * 100));
    }
    // Hysteresis is 3 seconds, so still AWAY
    assert(fsm.getCurrentState() == FocusState::AWAY);
    
    // More frames
    for (int i = 90; i < 110; ++i) {
        fsm.processFrame(createFrame(i * 100));
    }
    assert(fsm.getCurrentState() == FocusState::FOCUSED);
    std::cout << "test_hysteresis passed\n";
}

void test_looking_away() {
    FocusStateMachine fsm;
    // Calibrate
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));
    assert(fsm.getCurrentState() == FocusState::FOCUSED);
    
    // Looking away for 9 seconds (threshold is 8s)
    for (int i = 20; i < 110; ++i) {
        VisionResult r = createFrame(i * 100);
        r.head_pose.valid = true;
        r.head_pose.value.yaw = 35.0f; // > 30 degrees
        fsm.processFrame(r);
    }
    assert(fsm.getCurrentState() == FocusState::LOOKING_AWAY);
    std::cout << "test_looking_away passed\n";
}

void test_explicit_phone_usage_state_controls_focus_state() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));

    // A high phone_distraction_level without an explicit phone_usage_state
    // remains generic phone use.
    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 2000;
    sig.distraction_level = 3;
    fsm.injectPhoneSignal(sig);
    fsm.processFrame(createFrame(2100));
    assert(fsm.getCurrentState() == FocusState::PHONE_IN_USE);

    // Explicit phone_usage_state controls PHONE_IN_LEARNING / PHONE_IN_PLAYING.
    sig.usage_state = PhoneUsageState::LEARNING;
    sig.distraction_level = 1;
    sig.timestamp_ms = 2200;
    fsm.injectPhoneSignal(sig);
    fsm.processFrame(createFrame(2300));
    assert(fsm.getCurrentState() == FocusState::PHONE_IN_LEARNING);

    sig.usage_state = PhoneUsageState::PLAYING;
    sig.distraction_level = 2;
    sig.timestamp_ms = 2400;
    fsm.injectPhoneSignal(sig);
    fsm.processFrame(createFrame(2500));
    assert(fsm.getCurrentState() == FocusState::PHONE_IN_PLAYING);

    std::cout << "test_explicit_phone_usage_state_controls_focus_state passed\n";
}

void test_learning_phone_level_zero_is_low_risk_context() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));

    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 2000;
    sig.distraction_level = 0;
    sig.usage_state = PhoneUsageState::LEARNING;
    fsm.injectPhoneSignal(sig);
    fsm.processFrame(createFrame(2100));

    assert(fsm.getCurrentState() == FocusState::PHONE_IN_LEARNING);
    assert(fsm.getLastEvent().phone_signal);
    assert(fsm.getLastEvent().phone_distraction_level == 0);
    assert(fsm.getRiskScore() > 0);
    assert(fsm.getRiskScore() < 25);
    std::cout << "test_learning_phone_level_zero_is_low_risk_context passed\n";
}

void test_learning_phone_does_not_mask_looking_away() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));

    for (int i = 20; i < 120; ++i) {
        VisionResult r = createFrame(i * 100);
        r.head_pose.valid = true;
        r.head_pose.value.yaw = 35.0f;

        PhoneUsageSignal sig;
        sig.active = true;
        sig.timestamp_ms = r.timestamp_ms;
        sig.distraction_level = 0;
        sig.usage_state = PhoneUsageState::LEARNING;
        fsm.injectPhoneSignal(sig);
        fsm.processFrame(r);
    }

    assert(fsm.getLastEvent().phone_signal);
    assert(fsm.getLastEvent().is_looking_away);
    assert(fsm.getCurrentState() == FocusState::LOOKING_AWAY);
    std::cout << "test_learning_phone_does_not_mask_looking_away passed\n";
}

void test_phone_usage_state_modulates_fused_risk_score() {
    RiskScorer scorer;

    FocusEvent visual;
    visual.is_looking_away = true;
    visual.vision_signal = true;
    const int visual_score = scorer.calculateScore(visual);
    assert(visual_score > 0);

    FocusEvent learning = visual;
    learning.is_phone_in_use = true;
    learning.phone_signal = true;
    learning.phone_usage_state = PhoneUsageState::LEARNING;
    learning.phone_distraction_level = 0;
    const int learning_score = scorer.calculateScore(learning);
    assert(learning_score > 0);
    assert(learning_score < visual_score);
    assert(learning_score == (visual_score + 1) / 2);

    FocusEvent in_use = visual;
    in_use.is_phone_in_use = true;
    in_use.phone_signal = true;
    in_use.phone_usage_state = PhoneUsageState::IN_USE;
    in_use.phone_distraction_level = 0;

    FocusEvent in_use_level_three = in_use;
    in_use_level_three.phone_distraction_level = 3;

    FocusEvent playing = in_use;
    playing.phone_usage_state = PhoneUsageState::PLAYING;
    playing.phone_distraction_level = 2;

    const int in_use_score = scorer.calculateScore(in_use);
    const int in_use_level_three_score = scorer.calculateScore(in_use_level_three);
    const int playing_score = scorer.calculateScore(playing);
    assert(in_use_score > visual_score);
    assert(in_use_score == ((visual_score * 3) + 1) / 2);
    assert(in_use_level_three_score == in_use_score);
    assert(playing_score > in_use_score);
    assert(playing_score == visual_score * 2);
    assert(playing_score - in_use_score >= 10);

    std::cout << "test_phone_usage_state_modulates_fused_risk_score passed\n";
}

void test_calibration_quality_gate() {
    FocusStateMachine fsm;
    
    // Simulate 2 seconds of dark/bad frames
    for (int i = 0; i < 20; ++i) {
        VisionResult r = createFrame(i * 100);
        r.frame_quality.valid = true;
        r.frame_quality.level = FrameQualityLevel::DARK;
        fsm.processFrame(r);
    }
    // Should still be calibrating (quality gate blocks transition)
    assert(fsm.getCurrentState() == FocusState::CALIBRATING);
    
    // Now add good quality frames for 2 more seconds
    for (int i = 20; i < 40; ++i) {
        VisionResult r = createFrame(i * 100);
        r.frame_quality.valid = true;
        r.frame_quality.level = FrameQualityLevel::GOOD;
        fsm.processFrame(r);
    }
    // Should now be FOCUSED (good ratio >= 30%)
    assert(fsm.getCurrentState() == FocusState::FOCUSED);
    std::cout << "test_calibration_quality_gate passed\n";
}

void test_risk_score() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));
    assert(fsm.getRiskScore() == 0);
    
    // Phone use should increase risk
    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 2000;
    fsm.injectPhoneSignal(sig);
    fsm.processFrame(createFrame(2100));
    assert(fsm.getRiskScore() > 0);
    std::cout << "test_risk_score passed\n";
}

void test_last_event_tracks_multiple_signals() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));

    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 2000;
    sig.distraction_level = 2;
    fsm.injectPhoneSignal(sig);
    fsm.processFrame(createFrame(2100));

    // now produce enough frames to pass calibration and trigger vision signal
    for (int i = 21; i < 55; ++i) {
        VisionResult r = createFrame(i * 100);
        r.presence.value = Presence::AWAY;
        r.presence.confidence = 0.9f;
        r.head_pose.valid = true;
        r.head_pose.value.yaw = 35.0f;
        fsm.processFrame(r);
    }

    FocusEvent event = fsm.getLastEvent();
    assert(event.phone_signal);
    assert(event.vision_signal);
    assert(event.phone_distraction_level == 2);
    std::cout << "test_last_event_tracks_multiple_signals passed\n";
}

void test_event_detector_reset_clears_phone_signal() {
    EventDetector detector;
    BehaviorConfig config;
    SlidingWindow win(30000);

    VisionResult frame;
    frame.timestamp_ms = 1000;
    frame.presence = {Presence::PRESENT, 1.0f, true};
    frame.frame_quality.valid = true;
    frame.frame_quality.level = FrameQualityLevel::GOOD;
    win.add(frame);

    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 1000;
    sig.distraction_level = 3;
    detector.injectPhoneSignal(sig);

    auto with_phone = detector.detect(win, config);
    assert(with_phone.phone_signal);

    detector.resetPhoneSignal();
    auto after_reset = detector.detect(win, config);
    assert(!after_reset.phone_signal);
    assert(!after_reset.is_phone_in_use);

    std::cout << "test_event_detector_reset_clears_phone_signal passed\n";
}

void test_phone_signal_does_not_expire_by_ttl() {
    EventDetector detector;
    BehaviorConfig config;
    SlidingWindow win(30000);

    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 1000;
    sig.distraction_level = 2;
    sig.usage_state = PhoneUsageState::PLAYING;
    detector.injectPhoneSignal(sig);

    VisionResult late_frame = createFrame(9000);
    win.add(late_frame);

    auto event = detector.detect(win, config);
    assert(event.phone_signal);
    assert(event.is_phone_in_use);
    assert(event.phone_distraction_level == 2);
    assert(event.phone_usage_state == PhoneUsageState::PLAYING);
    std::cout << "test_phone_signal_does_not_expire_by_ttl passed\n";
}

void test_phone_signal_clear_does_not_force_focus_state() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));

    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = 2000;
    fsm.injectPhoneSignal(sig);
    fsm.processFrame(createFrame(2100));
    assert(fsm.getLastEvent().phone_signal);

    for (int i = 22; i < 56; ++i) {
        PhoneUsageSignal clear;
        clear.active = false;
        clear.timestamp_ms = i * 100;
        fsm.injectPhoneSignal(clear);

        VisionResult away = createFrame(i * 100);
        away.presence.value = Presence::AWAY;
        away.presence.confidence = 0.9f;
        fsm.processFrame(away);
    }

    assert(!fsm.getLastEvent().phone_signal);
    assert(fsm.getLastEvent().is_away);
    assert(fsm.getCurrentState() != FocusState::FOCUSED);
    std::cout << "test_phone_signal_clear_does_not_force_focus_state passed\n";
}

void test_visual_phone_and_hand_holding_phone_sets_phone_event() {
    FocusStateMachine fsm;
    for (int i = 0; i < 20; ++i) fsm.processFrame(createFrame(i * 100));

    VisionResult frame = createFrame(2100);
    frame.phone = {PhoneState::DETECTED, 0.9f, true};
    frame.hands = {HandState::HOLDING_PHONE, 0.9f, true};
    fsm.processFrame(frame);

    FocusEvent event = fsm.getLastEvent();
    assert(event.phone_signal);
    assert(event.is_phone_in_use);
    assert(event.risk_score > 0);
    std::cout << "test_visual_phone_and_hand_holding_phone_sets_phone_event passed\n";
}

int main() {
    test_focus();
    test_head_down();
    test_phone_in_use();
    test_away();
    test_low_confidence();
    test_phone_signal_survives_low_confidence_vision();
    test_hysteresis();
    test_looking_away();
    test_explicit_phone_usage_state_controls_focus_state();
    test_learning_phone_level_zero_is_low_risk_context();
    test_learning_phone_does_not_mask_looking_away();
    test_phone_usage_state_modulates_fused_risk_score();
    test_calibration_quality_gate();
    test_risk_score();
    test_last_event_tracks_multiple_signals();
    test_event_detector_reset_clears_phone_signal();
    test_phone_signal_does_not_expire_by_ttl();
    test_phone_signal_clear_does_not_force_focus_state();
    test_visual_phone_and_hand_holding_phone_sets_phone_event();
    std::cout << "All behavior FSM tests passed!\n";
    return 0;
}
