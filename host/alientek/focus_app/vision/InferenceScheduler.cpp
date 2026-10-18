#include "InferenceScheduler.h"

#include "app/Logger.h"

#include <chrono>
#include <thread>

namespace edgefocus {

VisionRuntimeMode parseVisionRuntimeMode(const char* value) {
    if (!value) return VisionRuntimeMode::AUTO;
    const std::string mode(value);
    if (mode == "mock") return VisionRuntimeMode::MOCK;
    if (mode == "real") return VisionRuntimeMode::REAL;
    if (mode == "fallback") return VisionRuntimeMode::FALLBACK;
    return VisionRuntimeMode::AUTO;
}

InferenceScheduler::InferenceScheduler(FrameQueue& frame_queue, ResultQueue& result_queue)
    : frame_queue_(frame_queue), result_queue_(result_queue) {}

bool InferenceScheduler::init(VisionRuntimeMode mode, const std::string& registry_path) {
    mode_ = mode;
    processed_frames_ = 0;
    dropped_results_ = 0;
    real_adapter_attempted_ = false;
    real_adapter_loaded_ = false;

    registry_.load(registry_path);
    fallback_adapter_.reset(new CpuFallbackPoseAdapter());
    fallback_adapter_->init(ModelPackage{});
    real_adapter_.reset();

    if (mode_ == VisionRuntimeMode::REAL && !registry_.hasPosePackage()) {
        LOGW("InferenceScheduler", "Real model requested but unavailable: %s", registry_.lastError().c_str());
    }
    LOGI("InferenceScheduler", "Init mode=%d real_pending=%d fallback_loaded=%d adapter=%s",
         static_cast<int>(mode_), registry_.hasPosePackage() ? 1 : 0, fallback_adapter_->loaded() ? 1 : 0, activeAdapterName());
    return true;
}

const char* InferenceScheduler::activeAdapterName() const {
    if (mode_ == VisionRuntimeMode::FALLBACK) return fallback_adapter_ ? fallback_adapter_->name() : "none";
    if (real_adapter_loaded_ && real_adapter_ && real_adapter_->loaded()) return real_adapter_->name();
    if (!real_adapter_attempted_ && registry_.hasPosePackage()) return "stai_mpu_pose_pending";
    return fallback_adapter_ ? fallback_adapter_->name() : "none";
}

bool InferenceScheduler::ensureRealAdapterLoaded() {
    if (real_adapter_loaded_ && real_adapter_ && real_adapter_->loaded()) {
        return true;
    }
    if (real_adapter_attempted_) {
        return false;
    }
    real_adapter_attempted_ = true;
    if (!registry_.hasPosePackage()) {
        LOGW("InferenceScheduler", "Real adapter unavailable: %s", registry_.lastError().c_str());
        return false;
    }

    LOGI("InferenceScheduler", "Lazy initializing real adapter in inference thread");
    real_adapter_.reset(new StaiMpuPoseAdapter());
    real_adapter_loaded_ = real_adapter_->init(registry_.posePackage());
    if (!real_adapter_loaded_) {
        LOGW("InferenceScheduler", "Real adapter init failed, falling back to CPU adapter");
        real_adapter_.reset();
    } else {
        LOGI("InferenceScheduler", "Real adapter initialized: %s", real_adapter_->name());
    }
    return real_adapter_loaded_;
}

ModelInference InferenceScheduler::runBestAdapter(const CameraFrame& frame, const FrameQualityMetrics& metrics) {
    if (mode_ != VisionRuntimeMode::FALLBACK && ensureRealAdapterLoaded()) {
        ModelInference result = real_adapter_->infer(frame, metrics);
        if (result.ran_model) {
            return result;
        }
    }
    if (fallback_adapter_ && fallback_adapter_->loaded()) {
        return fallback_adapter_->infer(frame, metrics);
    }
    ModelInference result;
    result.reason_code = ReasonCode::MODEL_NOT_LOADED;
    result.debug_note = "no model adapter available";
    return result;
}

VisionResult InferenceScheduler::buildResult(const CameraFrame& frame,
                                             const FrameQuality& quality,
                                             const ModelInference& inference) const {
    VisionResult result;
    result.timestamp_ms = frame.timestamp_ms;
    result.frame_quality = quality;
    result.reason_code = inference.reason_code;

    if (!quality.valid || quality.level != FrameQualityLevel::GOOD) {
        result.presence_conf = 0.0f;
        result.head_down_score = 0.0f;
        result.head_state = HeadState::NORMAL;
        result.presence = {Presence::UNCERTAIN, 0.20f, true};
        result.face = {FaceState::NOT_FOUND, 0.0f, false};
        result.head_pose = {HeadPose{}, 0.0f, false};
        result.eyes = {EyeState::NOT_VISIBLE, 0.0f, false};
        result.posture = {PostureType::NOT_VISIBLE, 0.0f, false};
        result.hands = {HandState::NOT_VISIBLE, 0.0f, false};
        result.phone = {PhoneState::UNCERTAIN, 0.0f, false};
        result.reason_code = ReasonCode::QUALITY_TOO_LOW;
        result.debug_note = "quality gate";
        return result;
    }

    // Map V1 fields
    result.presence_conf = inference.presence_conf;
    result.head_down_score = inference.head_down_score;
    result.head_state = inference.head_state;

    if (inference.person_detected) {
        // When real model ran but confidence is low (e.g. head not visible),
        // downgrade presence to UNCERTAIN
        if (inference.ran_model && inference.reason_code == ReasonCode::LOW_CONFIDENCE) {
            result.presence = {Presence::UNCERTAIN, inference.presence_conf, true};
        } else if (inference.presence_conf < 0.10f) {
            result.presence = {Presence::AWAY, inference.presence_conf, true};
        } else if (inference.presence_conf < 0.20f) {
            result.presence = {Presence::UNCERTAIN, inference.presence_conf, true};
        } else {
            result.presence = {Presence::PRESENT, inference.presence_conf, true};
        }
        result.face = {FaceState::DETECTED, inference.head_pose.valid ? inference.head_pose.confidence : 0.30f, inference.head_pose.valid};
    } else {
        result.presence = {Presence::AWAY, inference.presence_conf, true};
        result.face = {FaceState::NOT_FOUND, inference.person_confidence, true};
    }

    result.head_pose = inference.head_pose;
    result.eyes = inference.eyes;
    result.posture = inference.posture;
    result.hands = inference.hands;
    result.phone = inference.phone;
    result.debug_note = inference.debug_note;
    return result;
}

bool InferenceScheduler::runOnce() {
    auto frame = frame_queue_.popLatest();
    if (!frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return false;
    }

    FrameQualityMetrics metrics;
    const FrameQuality quality = quality_evaluator_.evaluate(*frame, &metrics);
    const ModelInference inference = runBestAdapter(*frame, metrics);
    VisionResult result = buildResult(*frame, quality, inference);
    if (!result_queue_.push(std::move(result))) {
        ++dropped_results_;
    }
    ++processed_frames_;

    if (processed_frames_ % 30 == 0) {
        LOGI("InferenceScheduler", "frames=%lld result_drops=%lld adapter=%s infer_ms=%.1f quality=%.2f",
             (long long)processed_frames_, (long long)dropped_results_, activeAdapterName(),
             inference.inference_time_ms, quality.score);
    }
    return true;
}

void InferenceScheduler::shutdown() {
    LOGI("InferenceScheduler", "Shutdown frames=%lld result_drops=%lld",
         (long long)processed_frames_, (long long)dropped_results_);
}

} // namespace edgefocus
