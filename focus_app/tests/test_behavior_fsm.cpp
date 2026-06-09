#include "behavior/FocusStateMachine.h"
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

int main() {
    test_focus();
    test_head_down();
    test_phone_in_use();
    test_away();
    test_low_confidence();
    test_hysteresis();
    std::cout << "All behavior FSM tests passed!\n";
    return 0;
}
