#include "vision/VisionResult.h"
#include "vision/VisionEngine.h"
#include "vision/InferenceScheduler.h"
#include "app/BoundedQueue.h"
#include <iostream>
#include <cassert>

using namespace edgefocus;

void test_confidence_value() {
    ConfidenceValue<int> val1;
    assert(val1.valid == false);
    assert(val1.confidence == 0.0f);

    ConfidenceValue<int> val2(42, 0.8f, true);
    assert(val2.valid == true);
    assert(val2.confidence == 0.8f);
    assert(val2.value == 42);
}

void test_vision_result_helpers() {
    VisionResult res;
    
    // Test isUsable
    assert(!res.isUsable());
    res.frame_quality.valid = true;
    res.frame_quality.level = FrameQualityLevel::GOOD;
    res.presence = {Presence::PRESENT, 0.6f, true};
    assert(res.isUsable());
    
    // Test isPresent
    assert(res.isPresent());
    
    // Test isAway
    res.presence = {Presence::AWAY, 0.8f, true};
    assert(!res.isPresent());
    assert(res.isAway());
    
    // Test isHeadDown
    assert(!res.isHeadDown());
    res.head_state = HeadState::DOWN;
    assert(res.isHeadDown());
    
    // Test isEyesClosed
    assert(!res.isEyesClosed());
    res.eyes = {EyeState::CLOSED, 0.6f, true};
    assert(res.isEyesClosed());
    
    // Test isPhoneDetected
    assert(!res.isPhoneDetected());
    res.phone = {PhoneState::DETECTED, 0.7f, true};
    assert(res.isPhoneDetected());
    
    // Test isLowConfidence
    assert(!res.isLowConfidence());
    res.reason_code = ReasonCode::LOW_CONFIDENCE;
    assert(res.isLowConfidence());
}

void test_mock_scenarios() {
    BoundedQueue<VisionResult> dummy_queue(1, false);
    VisionEngine engine(dummy_queue);
    
    // FOCUSED
    engine.setMockScenario(MockScenario::FOCUSED);
    engine.runOnce(*(AppContext*)nullptr);
    auto opt_focused = dummy_queue.tryPop();
    assert(opt_focused.has_value());
    VisionResult res_focused = *opt_focused;
    assert(res_focused.isPresent());
    assert(!res_focused.isAway());
    assert(!res_focused.isHeadDown());
    assert(!res_focused.isEyesClosed());
    assert(!res_focused.isPhoneDetected());
    assert(res_focused.isUsable());
    
    // HEAD_DOWN
    engine.setMockScenario(MockScenario::HEAD_DOWN);
    engine.runOnce(*(AppContext*)nullptr);
    auto opt_hd = dummy_queue.tryPop();
    assert(opt_hd.has_value());
    VisionResult res_hd = *opt_hd;
    assert(res_hd.isPresent());
    assert(res_hd.isHeadDown());
    assert(res_hd.reason_code == ReasonCode::HEAD_POSE_DOWN);
    
    // EYES_CLOSED
    engine.setMockScenario(MockScenario::EYES_CLOSED);
    engine.runOnce(*(AppContext*)nullptr);
    auto opt_ec = dummy_queue.tryPop();
    assert(opt_ec.has_value());
    VisionResult res_ec = *opt_ec;
    assert(res_ec.isPresent());
    assert(res_ec.isEyesClosed());
    assert(res_ec.reason_code == ReasonCode::EYES_CLOSED);
    
    // AWAY
    engine.setMockScenario(MockScenario::AWAY);
    engine.runOnce(*(AppContext*)nullptr);
    auto opt_away = dummy_queue.tryPop();
    assert(opt_away.has_value());
    VisionResult res_away = *opt_away;
    assert(res_away.isAway());
    assert(!res_away.isPresent());
    assert(res_away.reason_code == ReasonCode::NO_FACE_DETECTED);
    
    // PHONE_USE
    engine.setMockScenario(MockScenario::PHONE_USE);
    engine.runOnce(*(AppContext*)nullptr);
    auto opt_phone = dummy_queue.tryPop();
    assert(opt_phone.has_value());
    VisionResult res_phone = *opt_phone;
    assert(res_phone.isPresent());
    assert(res_phone.isPhoneDetected());
    assert(res_phone.reason_code == ReasonCode::PHONE_DETECTED);
    
    // LOW_QUALITY
    engine.setMockScenario(MockScenario::LOW_QUALITY);
    engine.runOnce(*(AppContext*)nullptr);
    auto opt_lq = dummy_queue.tryPop();
    assert(opt_lq.has_value());
    VisionResult res_lq = *opt_lq;
    assert(!res_lq.isUsable());
    assert(res_lq.reason_code == ReasonCode::QUALITY_TOO_LOW);
    
    // LOW_CONF
    engine.setMockScenario(MockScenario::LOW_CONF);
    engine.runOnce(*(AppContext*)nullptr);
    auto opt_lc = dummy_queue.tryPop();
    assert(opt_lc.has_value());
    VisionResult res_lc = *opt_lc;
    assert(res_lc.isLowConfidence());
    assert(res_lc.reason_code == ReasonCode::LOW_CONFIDENCE);
}

void test_fallback_inference_from_pixels() {
    BoundedQueue<CameraFrame> frame_queue(3, true);
    BoundedQueue<VisionResult> result_queue(5, false);
    InferenceScheduler scheduler(frame_queue, result_queue);
    assert(scheduler.init(VisionRuntimeMode::FALLBACK, "missing_registry.json"));

    CameraFrame frame;
    frame.timestamp_ms = 1234;
    frame.width = 64;
    frame.height = 48;
    frame.channels = 3;
    frame.format = PixelFormat::BGR24;
    frame.data.assign(static_cast<size_t>(frame.width * frame.height * frame.channels), 96);
    for (int y = 8; y < 42; ++y) {
        for (int x = 20; x < 44; ++x) {
            const size_t idx = static_cast<size_t>((y * frame.width + x) * frame.channels);
            frame.data[idx + 0] = 40;
            frame.data[idx + 1] = 155;
            frame.data[idx + 2] = 210;
        }
    }

    assert(frame_queue.push(std::move(frame)));
    assert(scheduler.runOnce());
    auto result = result_queue.tryPop();
    assert(result.has_value());
    assert(result->timestamp_ms == 1234);
    assert(result->frame_quality.valid);
    assert(result->frame_quality.level == FrameQualityLevel::GOOD);
    assert(result->presence.valid);
    assert(result->presence.value == Presence::PRESENT);
    assert(result->posture.valid);
    assert(!result->eyes.valid);
    assert(!result->hands.valid);
    assert(!result->phone.valid);
}

int main() {
    test_confidence_value();
    test_vision_result_helpers();
    test_mock_scenarios();
    test_fallback_inference_from_pixels();
    std::cout << "All VisionResult tests passed!\n";
    return 0;
}
