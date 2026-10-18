#pragma once
#include <cstdint>
#include <string>

namespace edgefocus {

// ---------------------------------------------------------------------------
// ConfidenceValue<T> — a tagged value that carries validity and confidence.
// All VisionResult sub-fields use this so the behavior layer can filter on
// quality and never treat low-confidence data as ground truth.
// ---------------------------------------------------------------------------

template <typename T>
struct ConfidenceValue {
    T value{};
    float confidence = 0.0f;  // 0.0 ~ 1.0
    bool valid = false;

    ConfidenceValue() = default;
    ConfidenceValue(T v, float c, bool ok) : value(v), confidence(c), valid(ok) {}
};

// ---------------------------------------------------------------------------
// Frame quality
// ---------------------------------------------------------------------------

enum class FrameQualityLevel : uint8_t {
    GOOD    = 0,
    BLURRY  = 1,
    DARK    = 2,
    BRIGHT  = 3,
    PARTIAL = 4,  // partially occluded or cropped
    UNKNOWN = 255
};

struct FrameQuality {
    FrameQualityLevel level = FrameQualityLevel::UNKNOWN;
    float score = 0.0f;       // 0.0 (bad) ~ 1.0 (good)
    bool valid = false;
    const char* reason = "";  // e.g. "blur detected", "too dark"
};

// ---------------------------------------------------------------------------
// Vision sub-result enums
// ---------------------------------------------------------------------------

enum class Presence : uint8_t {
    PRESENT   = 0,
    AWAY      = 1,
    UNCERTAIN = 2
};

enum class FaceState : uint8_t {
    DETECTED   = 0,
    NOT_FOUND  = 1,
    OCCLUDED   = 2,
    MULTIPLE   = 3
};

enum class HeadPoseDirection : uint8_t {
    FORWARD      = 0,
    DOWN         = 1,
    UP           = 2,
    LEFT         = 3,
    RIGHT        = 4,
    TILTED_LEFT  = 5,
    TILTED_RIGHT = 6
};

struct HeadPose {
    float yaw   = 0.0f;  // degrees, negative=left, positive=right
    float pitch = 0.0f;  // degrees, negative=down, positive=up
    float roll  = 0.0f;  // degrees
    HeadPoseDirection dominant = HeadPoseDirection::FORWARD;
};

enum class HeadState : uint8_t {
    NORMAL = 0,
    DOWN   = 1
};

enum class EyeState : uint8_t {
    OPEN      = 0,
    CLOSED    = 1,
    PARTIAL   = 2,
    NOT_VISIBLE = 3
};

enum class PostureType : uint8_t {
    UPRIGHT        = 0,
    LEANING_FORWARD = 1,
    LEANING_BACK   = 2,
    SLUMPED        = 3,
    NOT_VISIBLE    = 4
};

enum class HandState : uint8_t {
    ON_DESK      = 0,
    ON_KEYBOARD  = 1,
    HOLDING_PHONE = 2,
    RAISED       = 3,
    NOT_VISIBLE  = 4
};

enum class PhoneState : uint8_t {
    NOT_DETECTED = 0,
    DETECTED     = 1,
    UNCERTAIN    = 2
};

// ---------------------------------------------------------------------------
// Reason codes — explain why a VisionResult field has its value.
// Used for traceability and debugging.
// ---------------------------------------------------------------------------

enum class ReasonCode : uint16_t {
    NONE              = 0,
    QUALITY_TOO_LOW   = 1,
    NO_FACE_DETECTED  = 2,
    FACE_OCCLUDED     = 3,
    HEAD_POSE_DOWN    = 4,
    HEAD_POSE_AWAY    = 5,
    EYES_CLOSED       = 6,
    EYES_NOT_VISIBLE  = 7,
    POSTURE_SLUMPED   = 8,
    HAND_PHONE_HOLD   = 9,
    PHONE_DETECTED    = 10,
    LOW_CONFIDENCE    = 11,
    INFERENCE_TIMEOUT = 12,
    MODEL_NOT_LOADED  = 13,
    CAMERA_ERROR      = 14
};

// ---------------------------------------------------------------------------
// VisionResult — the core contract between vision and behavior layers.
//
// Rules (from CODING_STANDARDS.md):
// - Vision layer MUST NOT output "distracted" — that is the behavior layer's job.
// - Every field has confidence + valid.
// - timestamp_ms is monotonic clock, not wall clock.
// - reason_code explains the primary finding.
// ---------------------------------------------------------------------------

struct VisionResult {
    // Timestamp (monotonic milliseconds)
    int64_t timestamp_ms = 0;

    // Overall quality assessment
    FrameQuality frame_quality;

    // Is someone present in front of the camera?
    ConfidenceValue<Presence> presence;

    // Face detection
    ConfidenceValue<FaceState> face;

    // Head pose (only meaningful if face.valid)
    ConfidenceValue<HeadPose> head_pose;

    // Eye state (only meaningful if face.valid)
    ConfidenceValue<EyeState> eyes;

    // Body posture
    ConfidenceValue<PostureType> posture;

    // Hand state
    ConfidenceValue<HandState> hands;

    // Phone detection
    ConfidenceValue<PhoneState> phone;

    // V1 Logic Output fields
    float presence_conf = 0.0f;
    float head_down_score = 0.0f;
    HeadState head_state = HeadState::NORMAL;

    // Primary reason code for this result
    ReasonCode reason_code = ReasonCode::NONE;

    // Optional human-readable note (for debug logging only, not stored)
    std::string debug_note;

    // ---- Convenience helpers ----

    bool isUsable() const {
        return frame_quality.valid
            && frame_quality.level == FrameQualityLevel::GOOD
            && presence.valid
            && presence.confidence >= 0.5f;
    }

    bool isPresent() const {
        return presence.valid && presence.value == Presence::PRESENT;
    }

    bool isAway() const {
        return presence.valid && presence.value == Presence::AWAY;
    }

    bool isHeadDown() const {
        return head_state == HeadState::DOWN;
    }

    bool isEyesClosed() const {
        return eyes.valid && eyes.value == EyeState::CLOSED && eyes.confidence >= 0.5f;
    }

    bool isPhoneDetected() const {
        return phone.valid && phone.value == PhoneState::DETECTED && phone.confidence >= 0.6f;
    }

    bool isLowConfidence() const {
        return reason_code == ReasonCode::LOW_CONFIDENCE || 
               (!presence.valid) || 
               (presence.valid && presence.value == Presence::UNCERTAIN);
    }
};

} // namespace edgefocus
