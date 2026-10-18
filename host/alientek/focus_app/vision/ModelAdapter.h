#pragma once

#include "camera/CameraFrame.h"
#include "vision/FrameQuality.h"
#include "vision/ModelRegistry.h"
#include "vision/VisionResult.h"

#include <string>

namespace edgefocus {

struct ModelInference {
    bool model_loaded = false;
    bool ran_model = false;
    bool person_detected = false;
    float person_confidence = 0.0f;
    bool multiple_people = false;
    ConfidenceValue<HeadPose> head_pose;
    ConfidenceValue<PostureType> posture;
    ConfidenceValue<EyeState> eyes;
    ConfidenceValue<HandState> hands;
    ConfidenceValue<PhoneState> phone;
    
    // V1 fields
    float presence_conf = 0.0f;
    float head_down_score = 0.0f;
    HeadState head_state = HeadState::NORMAL;

    ReasonCode reason_code = ReasonCode::MODEL_NOT_LOADED;
    std::string debug_note;
    float inference_time_ms = 0.0f;
};

class ModelAdapter {
public:
    virtual ~ModelAdapter() = default;
    virtual bool init(const ModelPackage& package) = 0;
    virtual ModelInference infer(const CameraFrame& frame, const FrameQualityMetrics& metrics) = 0;
    virtual const char* name() const = 0;
    virtual bool loaded() const = 0;
};

} // namespace edgefocus
