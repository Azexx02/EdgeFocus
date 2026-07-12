#include "MockVisionSource.h"
#include <chrono>

namespace edgefocus {

VisionResult MockVisionSource::buildMockResult() const {
    VisionResult result;
    auto now = std::chrono::steady_clock::now();
    result.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    result.frame_quality.valid = true;
    result.frame_quality.score = 0.9f;
    result.frame_quality.level = FrameQualityLevel::GOOD;

    switch (scenario_) {
    case MockScenario::FOCUSED:
        result.presence_conf = 0.85f;
        result.head_down_score = 0.20f;
        result.head_state = HeadState::NORMAL;
        result.presence = {Presence::PRESENT, 0.95f, true};
        result.face = {FaceState::DETECTED, 0.90f, true};
        result.head_pose = {HeadPose{0.0f, 5.0f, 0.0f, HeadPoseDirection::FORWARD}, 0.85f, true};
        result.eyes = {EyeState::OPEN, 0.80f, true};
        result.posture = {PostureType::UPRIGHT, 0.80f, true};
        result.hands = {HandState::ON_DESK, 0.70f, true};
        result.phone = {PhoneState::NOT_DETECTED, 0.60f, true};
        result.reason_code = ReasonCode::NONE;
        result.debug_note = "mock: focused";
        break;

    case MockScenario::HEAD_DOWN:
        result.presence_conf = 0.80f;
        result.head_down_score = 0.85f;
        result.head_state = HeadState::DOWN;
        result.presence = {Presence::PRESENT, 0.90f, true};
        result.face = {FaceState::DETECTED, 0.85f, true};
        result.head_pose = {HeadPose{0.0f, -35.0f, 0.0f, HeadPoseDirection::DOWN}, 0.85f, true};
        result.eyes = {EyeState::OPEN, 0.70f, true};
        result.posture = {PostureType::LEANING_FORWARD, 0.75f, true};
        result.hands = {HandState::ON_DESK, 0.60f, true};
        result.phone = {PhoneState::NOT_DETECTED, 0.50f, true};
        result.reason_code = ReasonCode::HEAD_POSE_DOWN;
        result.debug_note = "mock: head down";
        break;

    case MockScenario::EYES_CLOSED:
        result.presence_conf = 0.80f;
        result.head_down_score = 0.30f;
        result.head_state = HeadState::NORMAL;
        result.presence = {Presence::PRESENT, 0.90f, true};
        result.face = {FaceState::DETECTED, 0.85f, true};
        result.head_pose = {HeadPose{0.0f, -5.0f, 0.0f, HeadPoseDirection::FORWARD}, 0.80f, true};
        result.eyes = {EyeState::CLOSED, 0.85f, true};
        result.posture = {PostureType::UPRIGHT, 0.70f, true};
        result.hands = {HandState::ON_DESK, 0.60f, true};
        result.phone = {PhoneState::NOT_DETECTED, 0.50f, true};
        result.reason_code = ReasonCode::EYES_CLOSED;
        result.debug_note = "mock: eyes closed";
        break;

    case MockScenario::AWAY:
        result.presence_conf = 0.05f;
        result.head_down_score = 0.0f;
        result.head_state = HeadState::NORMAL;
        result.presence = {Presence::AWAY, 0.90f, true};
        result.face = {FaceState::NOT_FOUND, 0.85f, true};
        result.head_pose = {HeadPose{}, 0.0f, false};
        result.eyes = {EyeState::NOT_VISIBLE, 0.0f, false};
        result.posture = {PostureType::NOT_VISIBLE, 0.0f, false};
        result.hands = {HandState::NOT_VISIBLE, 0.0f, false};
        result.phone = {PhoneState::NOT_DETECTED, 0.30f, false};
        result.reason_code = ReasonCode::NO_FACE_DETECTED;
        result.debug_note = "mock: away";
        break;

    case MockScenario::PHONE_USE:
        result.presence_conf = 0.80f;
        result.head_down_score = 0.50f;
        result.head_state = HeadState::NORMAL;
        result.presence = {Presence::PRESENT, 0.90f, true};
        result.face = {FaceState::DETECTED, 0.80f, true};
        result.head_pose = {HeadPose{0.0f, -25.0f, 0.0f, HeadPoseDirection::DOWN}, 0.80f, true};
        result.eyes = {EyeState::OPEN, 0.70f, true};
        result.posture = {PostureType::LEANING_FORWARD, 0.75f, true};
        result.hands = {HandState::HOLDING_PHONE, 0.85f, true};
        result.phone = {PhoneState::DETECTED, 0.90f, true};
        result.reason_code = ReasonCode::PHONE_DETECTED;
        result.debug_note = "mock: phone use";
        break;

    case MockScenario::LOW_QUALITY:
        result.frame_quality.score = 0.2f;
        result.frame_quality.level = FrameQualityLevel::BLURRY;
        result.frame_quality.reason = "mock: simulated blur";
        result.presence_conf = 0.15f;
        result.head_down_score = 0.0f;
        result.head_state = HeadState::NORMAL;
        result.presence = {Presence::UNCERTAIN, 0.30f, true};
        result.face = {FaceState::NOT_FOUND, 0.20f, false};
        result.head_pose = {HeadPose{}, 0.0f, false};
        result.eyes = {EyeState::NOT_VISIBLE, 0.0f, false};
        result.posture = {PostureType::NOT_VISIBLE, 0.0f, false};
        result.hands = {HandState::NOT_VISIBLE, 0.0f, false};
        result.phone = {PhoneState::UNCERTAIN, 0.10f, false};
        result.reason_code = ReasonCode::QUALITY_TOO_LOW;
        result.debug_note = "mock: low quality frame";
        break;

    case MockScenario::LOW_CONF:
        result.presence_conf = 0.15f;
        result.head_down_score = 0.10f;
        result.head_state = HeadState::NORMAL;
        result.presence = {Presence::PRESENT, 0.25f, true};
        result.face = {FaceState::DETECTED, 0.20f, true};
        result.head_pose = {HeadPose{0.0f, 0.0f, 0.0f, HeadPoseDirection::FORWARD}, 0.15f, true};
        result.eyes = {EyeState::PARTIAL, 0.20f, true};
        result.posture = {PostureType::UPRIGHT, 0.15f, true};
        result.hands = {HandState::NOT_VISIBLE, 0.10f, false};
        result.phone = {PhoneState::UNCERTAIN, 0.10f, false};
        result.reason_code = ReasonCode::LOW_CONFIDENCE;
        result.debug_note = "mock: low confidence";
        break;
    }

    return result;
}

} // namespace edgefocus
