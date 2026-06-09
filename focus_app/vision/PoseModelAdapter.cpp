#include "PoseModelAdapter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#ifdef EDGEFOCUS_WITH_OPENCV
#include <opencv2/imgproc.hpp>
#endif

#if defined(EDGEFOCUS_WITH_STAI_MPU) && defined(EDGEFOCUS_WITH_OPENCV)
#include "stai_mpu_wrapper.hpp"
#include "pose_keypoint_pp.hpp"
#endif

namespace edgefocus {

namespace {

int64_t nowMs() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float lumaAt(const CameraFrame& frame, int x, int y) {
    const size_t idx = static_cast<size_t>((y * frame.width + x) * frame.channels);
    if (frame.channels == 1) return static_cast<float>(frame.data[idx]);
    const float b = static_cast<float>(frame.data[idx + 0]);
    const float g = static_cast<float>(frame.data[idx + 1]);
    const float r = static_cast<float>(frame.data[idx + 2]);
    return 0.114f * b + 0.587f * g + 0.299f * r;
}

float centralActivityScore(const CameraFrame& frame, const FrameQualityMetrics& metrics) {
    if (!frame.valid()) return 0.0f;
    const int x0 = frame.width / 4;
    const int x1 = frame.width * 3 / 4;
    const int y0 = frame.height / 6;
    const int y1 = frame.height * 5 / 6;
    const int step_x = std::max(1, (x1 - x0) / 32);
    const int step_y = std::max(1, (y1 - y0) / 32);
    int active = 0;
    int count = 0;
    for (int y = y0; y < y1; y += step_y) {
        for (int x = x0; x < x1; x += step_x) {
            const float luma = lumaAt(frame, x, y);
            if (std::fabs(luma - metrics.mean_luma) > std::max(16.0f, metrics.contrast * 0.45f)) {
                ++active;
            }
            ++count;
        }
    }
    if (count == 0) return 0.0f;
    return clamp01(static_cast<float>(active) / static_cast<float>(count) * 2.2f);
}

HeadPoseDirection dominantFromPitch(float pitch) {
    if (pitch < -20.0f) return HeadPoseDirection::DOWN;
    if (pitch > 20.0f) return HeadPoseDirection::UP;
    return HeadPoseDirection::FORWARD;
}

} // namespace

bool CpuFallbackPoseAdapter::init(const ModelPackage& /*package*/) {
    loaded_ = true;
    return true;
}

ModelInference CpuFallbackPoseAdapter::infer(const CameraFrame& frame, const FrameQualityMetrics& metrics) {
    const int64_t start = nowMs();
    ModelInference out;
    out.model_loaded = loaded_;
    out.ran_model = false;
    out.eyes = {EyeState::NOT_VISIBLE, 0.0f, false};
    out.hands = {HandState::NOT_VISIBLE, 0.0f, false};
    out.phone = {PhoneState::UNCERTAIN, 0.0f, false};

    const float activity = centralActivityScore(frame, metrics);
    out.person_detected = activity > 0.22f && metrics.contrast > 10.0f;
    out.person_confidence = out.person_detected ? clamp01(0.35f + activity * 0.55f) : clamp01(0.25f + activity * 0.35f);
    out.reason_code = out.person_detected ? ReasonCode::LOW_CONFIDENCE : ReasonCode::NO_FACE_DETECTED;

    if (out.person_detected) {
        const float upper = lumaAt(frame, frame.width / 2, std::max(0, frame.height / 4));
        const float middle = lumaAt(frame, frame.width / 2, frame.height / 2);
        const float pitch = std::clamp((middle - upper) * -0.35f, -30.0f, 20.0f);
        HeadPose pose;
        pose.pitch = pitch;
        pose.dominant = dominantFromPitch(pitch);
        out.head_pose = {pose, 0.35f, true};
        out.posture = {pitch < -18.0f ? PostureType::LEANING_FORWARD : PostureType::UPRIGHT, 0.38f, true};
        
        // V1 fields fallback
        out.presence_conf = clamp01(out.person_confidence);
        out.head_down_score = pitch < -18.0f ? 0.8f : 0.0f;
        out.head_state = out.head_down_score > 0.70f ? HeadState::DOWN : HeadState::NORMAL;

        out.reason_code = pitch < -20.0f ? ReasonCode::HEAD_POSE_DOWN : ReasonCode::LOW_CONFIDENCE;
    }

    out.debug_note = "cpu fallback from real frame";
    out.inference_time_ms = static_cast<float>(nowMs() - start);
    return out;
}

struct StaiMpuPoseAdapter::Impl {
#if defined(EDGEFOCUS_WITH_STAI_MPU) && defined(EDGEFOCUS_WITH_OPENCV)
    wrapper_stai_mpu::stai_mpu_wrapper wrapper;
    wrapper_stai_mpu::Config config;
    float confidence_threshold = 0.65f;
    float iou_threshold = 0.50f;
    int input_width = 0;
    int input_height = 0;
#endif
};

StaiMpuPoseAdapter::StaiMpuPoseAdapter() : impl_(new Impl()) {}
StaiMpuPoseAdapter::~StaiMpuPoseAdapter() = default;

bool StaiMpuPoseAdapter::init(const ModelPackage& package) {
#if defined(EDGEFOCUS_WITH_STAI_MPU) && defined(EDGEFOCUS_WITH_OPENCV)
    if (!std::filesystem::exists(package.model_path)) {
        loaded_ = false;
        return false;
    }
    impl_->config.model_name = package.model_path;
    impl_->config.number_of_threads = 2;
    impl_->config.number_of_results = 5;
    impl_->confidence_threshold = package.confidence_threshold;
    impl_->iou_threshold = package.iou_threshold;
    impl_->wrapper.Initialize(&impl_->config);
    impl_->input_width = impl_->wrapper.GetInputWidth();
    impl_->input_height = impl_->wrapper.GetInputHeight();
    loaded_ = true;
    return true;
#else
    (void)package;
    loaded_ = false;
    return false;
#endif
}

ModelInference StaiMpuPoseAdapter::infer(const CameraFrame& frame, const FrameQualityMetrics& /*metrics*/) {
    ModelInference out;
    out.model_loaded = loaded_;
    out.eyes = {EyeState::NOT_VISIBLE, 0.0f, false};
    out.hands = {HandState::NOT_VISIBLE, 0.0f, false};
    out.phone = {PhoneState::UNCERTAIN, 0.0f, false};

#if defined(EDGEFOCUS_WITH_STAI_MPU) && defined(EDGEFOCUS_WITH_OPENCV)
    if (!loaded_ || !frame.valid()) {
        out.reason_code = ReasonCode::MODEL_NOT_LOADED;
        out.debug_note = "stai unavailable";
        return out;
    }

    cv::Mat source(frame.height, frame.width, frame.channels == 1 ? CV_8UC1 : CV_8UC3,
                   const_cast<uint8_t*>(frame.data.data()));
    cv::Mat infer_image;
    cv::resize(source, infer_image, cv::Size(impl_->input_width, impl_->input_height));

    const int64_t start = nowMs();
    impl_->wrapper.RunInference(infer_image.data);
    out.inference_time_ms = impl_->wrapper.GetInferenceTime();

    float* outputs = static_cast<float*>(impl_->wrapper.m_stai_mpu_model->get_output(0));
    const auto detections = nn_postproc::postprocessyolov8_pose(outputs, impl_->confidence_threshold, impl_->iou_threshold);
    free(outputs);

    out.ran_model = true;
    out.inference_time_ms = out.inference_time_ms > 0.0f ? out.inference_time_ms : static_cast<float>(nowMs() - start);
    if (detections.empty()) {
        out.person_detected = false;
        out.person_confidence = 0.85f;
        out.reason_code = ReasonCode::NO_FACE_DETECTED;
        out.debug_note = "stai pose: no person";
        return out;
    }

    const auto best = *std::max_element(detections.begin(), detections.end(),
        [](const nn_postproc::ObjDetect_Results& a, const nn_postproc::ObjDetect_Results& b) {
            return a.confidence < b.confidence;
        });
    out.person_detected = true;
    out.multiple_people = detections.size() > 1;
    out.person_confidence = clamp01(best.confidence);

    const auto& k = best.keypoints;
    auto score = [&k](size_t idx) { return idx * 3 + 2 < k.size() ? k[idx * 3 + 2] : 0.0f; };
    auto y = [&k](size_t idx) { return idx * 3 + 1 < k.size() ? k[idx * 3 + 1] : 0.0f; };
    
    // 1. Calculate presence_conf
    int visible_count = 0;
    const size_t check_indices[] = {0, 1, 2, 5, 6}; // nose, left_eye, right_eye, left_shoulder, right_shoulder
    for (size_t idx : check_indices) {
        if (score(idx) > 0.25f) visible_count++;
    }
    out.presence_conf = clamp01(out.person_confidence * (visible_count / 5.0f));

    // 2. Calculate head_down_score
    const float nose_s = score(0);
    const float shoulder_s = std::max(score(5), score(6));
    const bool head_visible = nose_s >= 0.25f && shoulder_s >= 0.25f;
    
    if (head_visible) {
        const float shoulder_y = (score(5) >= 0.25f && score(6) >= 0.25f) ? (y(5) + y(6)) * 0.5f : ((score(5) >= 0.25f) ? y(5) : y(6));
        const float bbox_h = std::max(1.0f, best.height);
        out.head_down_score = clamp01(1.0f - (shoulder_y - y(0)) / (bbox_h * 0.30f));
        out.head_state = out.head_down_score > 0.70f ? HeadState::DOWN : HeadState::NORMAL;

        // Keep legacy fields for future eye/face models
        const float pitch = std::clamp((0.20f - (shoulder_y - y(0))) * 140.0f, -35.0f, 25.0f);
        HeadPose pose;
        pose.pitch = pitch;
        pose.dominant = dominantFromPitch(pitch);
        out.head_pose = {pose, clamp01((nose_s + shoulder_s) * 0.5f), true};
        out.posture = {pitch < -18.0f ? PostureType::LEANING_FORWARD : PostureType::UPRIGHT,
                       clamp01(shoulder_s), true};
        out.reason_code = out.head_state == HeadState::DOWN ? ReasonCode::HEAD_POSE_DOWN : ReasonCode::NONE;
    } else {
        out.head_down_score = 0.0f;
        out.head_state = HeadState::NORMAL;
        
        out.head_pose = {HeadPose{}, 0.0f, false};
        out.posture = {PostureType::NOT_VISIBLE, 0.0f, false};
        out.reason_code = ReasonCode::LOW_CONFIDENCE;
    }
    out.debug_note = out.multiple_people ? "stai pose: multiple people" : "stai pose";
    return out;
#else
    (void)frame;
    out.reason_code = ReasonCode::MODEL_NOT_LOADED;
    out.debug_note = "stai not compiled";
    return out;
#endif
}

} // namespace edgefocus
