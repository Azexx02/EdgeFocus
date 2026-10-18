#include "vision/DistractionPipelineFeature.h"

#include "app/SystemStateStore.h"
#include "distraction/portable_model_json.hpp"
#include "distraction/stai_pose_runtime.hpp"
#include "distraction/temporal.hpp"
#include "distraction/vision.hpp"
#include "vision/VisionCadencePolicy.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace edgefocus {
namespace {

#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
double frame_quality(const cv::Mat& frame) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::Scalar mean, deviation;
    cv::meanStdDev(gray, mean, deviation);
    const double brightness = std::clamp(
        std::min(mean[0] / 40.0, (255.0 - mean[0]) / 40.0), 0.0, 1.0);
    const double contrast = std::clamp(deviation[0] / 20.0, 0.0, 1.0);
    return brightness * (0.6 + 0.4 * contrast);
}

cv::Mat camera_frame_view(const CameraFrame& frame) {
    if (!frame.valid()) throw std::invalid_argument("invalid camera frame");
    const std::size_t expected = static_cast<std::size_t>(frame.width) * frame.height * frame.channels;
    if (frame.data.size() < expected) throw std::invalid_argument("camera frame buffer is truncated");

    cv::Mat view;
    if (frame.channels == 1 && frame.format == PixelFormat::GRAY8) {
        cv::Mat gray(frame.height, frame.width, CV_8UC1, const_cast<std::uint8_t*>(frame.data.data()));
        cv::cvtColor(gray, view, cv::COLOR_GRAY2BGR);
    } else if (frame.channels == 3) {
        cv::Mat packed(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(frame.data.data()));
        if (frame.format == PixelFormat::RGB24) {
            cv::cvtColor(packed, view, cv::COLOR_RGB2BGR);
        } else if (frame.format == PixelFormat::BGR24) {
            view = packed;
        } else {
            throw std::invalid_argument("unsupported three-channel pixel format");
        }
    } else {
        throw std::invalid_argument("unsupported camera frame layout");
    }
    return view;
}

std::vector<float> preprocess(const cv::Mat& frame, const distraction::LetterboxMeta& meta) {
    cv::Mat resized, padded, rgb;
    cv::resize(frame, resized, cv::Size(meta.resized_width, meta.resized_height), 0, 0, cv::INTER_LINEAR);
    cv::copyMakeBorder(
        resized,
        padded,
        meta.pad_top,
        meta.pad_bottom,
        meta.pad_left,
        meta.pad_right,
        cv::BORDER_CONSTANT,
        cv::Scalar(114, 114, 114));
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    const int size = meta.target_width;
    std::vector<float> result(3ULL * size * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const auto pixel = rgb.at<cv::Vec3b>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                result[static_cast<std::size_t>(channel) * size * size + y * size + x] =
                    pixel[channel] / 255.0F;
            }
        }
    }
    return result;
}
#endif

int legacy_risk_score(const std::optional<int>& level) {
    if (!level || *level <= 0) return 0;
    if (*level == 1) return 25;
    if (*level == 2) return 60;
    return 90;
}

std::string join_reasons(const std::vector<std::string>& reasons) {
    std::ostringstream output;
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index) output << ',';
        output << reasons[index];
    }
    return output.str();
}

}  // namespace

struct DistractionPipelineFeature::Impl {
#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
    std::unique_ptr<distraction::StaiPoseRuntime> pose;
    std::unique_ptr<distraction::OnlineProneClassifier> prone;
    std::optional<distraction::Box> previous_box;
#endif
};

DistractionPipelineFeature::DistractionPipelineFeature(
    FrameQueue& frames,
    PhoneFactBuffer& phone_facts,
    session::SessionSnapshotPublisher& snapshots,
    SystemStateStore& compatibility_store,
    std::string pose_model_path,
    std::string prone_model_path,
    bool publish_compatibility,
    const power::WorkloadProfilePolicy* workload_policy)
    : frames_(frames),
      phone_facts_(phone_facts),
      snapshots_(snapshots),
      compatibility_store_(compatibility_store),
      workload_policy_(workload_policy),
      pose_model_path_(pose_model_path.empty()
                           ? configuredPath("EDGEFOCUS_POSE_MODEL", "models/distraction/yolo11n_pose_256_float.nb")
                           : std::move(pose_model_path)),
      prone_model_path_(prone_model_path.empty()
                            ? configuredPath("EDGEFOCUS_PRONE_MODEL", "models/distraction/prone_mlp_v1.json")
                            : std::move(prone_model_path)),
      impl_(std::make_unique<Impl>()),
      publish_compatibility_(publish_compatibility) {}

DistractionPipelineFeature::~DistractionPipelineFeature() {
    stop(session::StopReason::ProcessShutdown);
}

std::string DistractionPipelineFeature::configuredPath(const char* env_name, const char* fallback) {
    const char* value = std::getenv(env_name);
    return value && *value ? value : fallback;
}

std::int64_t DistractionPipelineFeature::monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void DistractionPipelineFeature::setHealth(
    bool available,
    bool degraded,
    std::string status,
    std::string detail) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    health_ = {available, degraded, std::move(status), std::move(detail), monotonicMs()};
}

session::PrepareResult DistractionPipelineFeature::prepare(const session::SessionContext& context) {
    if (running_.load()) return session::PrepareResult::failure("pipeline already running");
    context_ = context;
    formal_started_ = false;
    phase_.store(session::Phase::Idle);
    last_scheduled_inference_ms_ = 0;
    last_inference_frame_ms_ = 0;
    inference_fps_ = 0.0;

#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
    try {
        impl_->pose = std::make_unique<distraction::StaiPoseRuntime>(pose_model_path_);
        impl_->prone = std::make_unique<distraction::OnlineProneClassifier>(
            distraction::load_portable_mlp_json(prone_model_path_, true));
        impl_->previous_box.reset();
    } catch (const std::exception& error) {
        setHealth(false, true, "prepare_failed", error.what());
        return session::PrepareResult::failure(error.what());
    }

    running_.store(true);
    worker_ = std::thread(&DistractionPipelineFeature::workerLoop, this);
    setHealth(true, false, "ready");
    return session::PrepareResult::success();
#else
    const std::string detail = "focus_app requires OpenCV and STAI MPU for the production pipeline";
    setHealth(false, true, "not_compiled", detail);
    return session::PrepareResult::failure(detail);
#endif
}

session::PrepareResult DistractionPipelineFeature::beginFormalSession(
    const session::FormalStartContext& /*formal_start*/) {
    std::lock_guard<std::mutex> lock(processing_mutex_);
    if (!running_.load()) return session::PrepareResult::failure("pipeline is not running");
    if (formal_started_) return session::PrepareResult::success();
    while (frames_.tryPop().has_value()) {
    }
    phone_facts_.beginSession(context_.session_id);
    assessment_engine_.beginSession(context_.session_id);
    assessment_engine_.resetContinuity();
#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
    impl_->previous_box.reset();
    if (impl_->prone) impl_->prone->reset();
#endif
    last_scheduled_inference_ms_ = 0;
    last_inference_frame_ms_ = 0;
    inference_fps_ = 0.0;
    formal_started_ = true;
    return session::PrepareResult::success();
}

void DistractionPipelineFeature::enterPhase(session::Phase phase) {
    std::lock_guard<std::mutex> lock(processing_mutex_);
    const auto previous = phase_.load();
    if (phase == session::Phase::Focusing && previous != session::Phase::Focusing) {
        assessment_engine_.resetContinuity();
#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
        if (impl_->prone) impl_->prone->reset();
#endif
    }
    phase_.store(phase);
}

void DistractionPipelineFeature::stop(session::StopReason /*reason*/) noexcept {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(processing_mutex_);
    while (frames_.tryPop().has_value()) {
    }
    if (formal_started_) {
        phone_facts_.endSession();
        assessment_engine_.endSession();
        formal_started_ = false;
    }
#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
    impl_->previous_box.reset();
    impl_->prone.reset();
    impl_->pose.reset();
#endif
    phase_.store(session::Phase::Idle);
    setHealth(false, false, "stopped");
}

session::FeatureHealth DistractionPipelineFeature::health() const {
    std::lock_guard<std::mutex> lock(health_mutex_);
    return health_;
}

void DistractionPipelineFeature::resetContinuity() {
    std::lock_guard<std::mutex> lock(processing_mutex_);
    assessment_engine_.resetContinuity();
    while (frames_.tryPop().has_value()) {
    }
#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
    impl_->previous_box.reset();
    if (impl_->prone) impl_->prone->reset();
#endif
    last_inference_frame_ms_ = 0;
    last_scheduled_inference_ms_ = 0;
    inference_fps_ = 0.0;
}

void DistractionPipelineFeature::workerLoop() {
    while (running_.load()) {
        auto frame = frames_.tryPop();
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }
        while (auto newer = frames_.tryPop()) {
            frame = std::move(newer);
        }
        const auto phase = phase_.load();
        if (phase != session::Phase::Precheck && phase != session::Phase::Focusing &&
            phase != session::Phase::Paused) {
            continue;
        }
        const auto profile = workload_policy_
                                 ? workload_policy_->snapshot().effective_profile
                                 : power::WorkloadProfile::Home;
        const auto interval_ms = vision::inferenceIntervalMs(phase, profile);
        if (last_scheduled_inference_ms_ > 0 &&
            frame->timestamp_ms >= last_scheduled_inference_ms_ &&
            frame->timestamp_ms - last_scheduled_inference_ms_ < interval_ms) {
            continue;
        }
        last_scheduled_inference_ms_ = frame->timestamp_ms;
        try {
            std::lock_guard<std::mutex> lock(processing_mutex_);
            processFrame(*frame, phase);
            setHealth(true, false, "running");
        } catch (const std::exception& error) {
            setHealth(true, true, "frame_error", error.what());
        } catch (...) {
            setHealth(true, true, "frame_error", "unknown inference error");
        }
    }
}

void DistractionPipelineFeature::processFrame(const CameraFrame& frame, session::Phase phase) {
#if defined(EDGEFOCUS_WITH_OPENCV) && defined(EDGEFOCUS_WITH_STAI_MPU)
    if (!impl_->pose || !impl_->prone) throw std::logic_error("pipeline model is not ready");
    const cv::Mat image = camera_frame_view(frame);
    std::optional<EvidenceValue> presence;
    std::optional<EvidenceValue> prone;
    session::VisualSnapshot visual;
    visual.evidence_timestamp_ms = frame.timestamp_ms;
    visual.frame_id = static_cast<std::uint64_t>(std::max<std::int64_t>(0, frame.timestamp_ms));
    visual.media_timestamp_ms = frame.media_timestamp_ms;
    visual.source = frame.source;
    const auto inference_started_ms = monotonicMs();

    if (frame_quality(image) >= 0.20) {
        const auto meta = distraction::make_letterbox(
            image.cols, image.rows, impl_->pose->contract().input_size);
        const auto result = impl_->pose->run(preprocess(image, meta));
        const auto detections = distraction::decode_yolo_pose(
            result.bcn_output,
            meta,
            0.35,
            0.70,
            300,
            impl_->pose->contract().output_candidates);
        auto subject = distraction::select_seat_subject(
            detections,
            image.cols,
            image.rows,
            {0.10, 0.05, 0.90, 0.98},
            impl_->previous_box);
        if (!subject) {
            impl_->previous_box.reset();
            presence = EvidenceValue{frame.timestamp_ms, 0.0, true, 500};
            visual.presence = 0.0;
        } else {
            impl_->previous_box = subject->normalized_box;
            presence = EvidenceValue{frame.timestamp_ms, 1.0, true, 500};
            visual.presence = 1.0;
            visual.subject_box = session::VisualBoxSnapshot{
                subject->normalized_box.x1,
                subject->normalized_box.y1,
                subject->normalized_box.x2,
                subject->normalized_box.y2};
            visual.subject_confidence = subject->detection.confidence;
            visual.keypoints.reserve(subject->detection.keypoints.size());
            for (const auto& point : subject->detection.keypoints) {
                visual.keypoints.push_back(session::VisualKeypointSnapshot{
                    std::clamp(point.x / std::max(1, image.cols), 0.0, 1.0),
                    std::clamp(point.y / std::max(1, image.rows), 0.0, 1.0),
                    point.confidence});
            }
            if (phase != session::Phase::Paused) {
                const auto features = distraction::extract_keypoint_features58(
                    subject->detection, image.cols, image.rows, 0.62, 0.25);
                if (features.valid_for_prone) {
                    const std::vector<double> values(features.values.begin(), features.values.end());
                    const auto prone_score = impl_->prone->update(frame.timestamp_ms, values);
                    if (prone_score) {
                        prone = EvidenceValue{frame.timestamp_ms, *prone_score, true, 500};
                        visual.prone = *prone_score;
                    }
                }
            }
        }
    }

    const auto inference_finished_ms = monotonicMs();
    visual.inference_latency_ms = static_cast<double>(
        std::max<std::int64_t>(0, inference_finished_ms - inference_started_ms));
    if (last_inference_frame_ms_ > 0 && frame.timestamp_ms > last_inference_frame_ms_) {
        const double instant_fps = 1000.0 / (frame.timestamp_ms - last_inference_frame_ms_);
        inference_fps_ = inference_fps_ <= 0.0
                             ? instant_fps
                             : inference_fps_ * 0.8 + instant_fps * 0.2;
    }
    last_inference_frame_ms_ = frame.timestamp_ms;
    visual.inference_fps = inference_fps_;
    const auto phone_snapshot = formal_started_
                                    ? phone_facts_.snapshot(frame.timestamp_ms)
                                    : session::PhoneSnapshot{};
    std::optional<session::DistractionAssessment> assessment;

    if (phase == session::Phase::Focusing && formal_started_) {
        assessment = assessment_engine_.update(
            frame.timestamp_ms,
            presence,
            prone,
            phone_facts_.observation(frame.timestamp_ms));
        if (publish_compatibility_) publishCompatibility(*assessment, phone_snapshot);
    }
    snapshots_.updateEvidence(visual, phone_snapshot, assessment, frame.timestamp_ms);
#else
    (void)frame;
    (void)phase;
    throw std::logic_error("production pipeline not compiled");
#endif
}

void DistractionPipelineFeature::publishCompatibility(
    const session::DistractionAssessment& assessment,
    const session::PhoneSnapshot& phone) {
    BehaviorContext compatibility;
    compatibility.risk_score = legacy_risk_score(assessment.level);
    compatibility.vision_signal = std::find(
                                      assessment.reasons.begin(),
                                      assessment.reasons.end(),
                                      "PRONE") != assessment.reasons.end();
    compatibility.phone_signal = phone.fresh && phone.active;
    compatibility.phone_distraction_level = assessment.level.value_or(0);
    if (phone.category == session::PhoneCategory::Learning) {
        compatibility.phone_usage_state = static_cast<int>(PhoneUsageState::LEARNING);
    } else if (phone.category == session::PhoneCategory::Entertainment) {
        compatibility.phone_usage_state = static_cast<int>(PhoneUsageState::PLAYING);
    } else if (phone.category == session::PhoneCategory::GeneralOrUnknown) {
        compatibility.phone_usage_state = static_cast<int>(PhoneUsageState::IN_USE);
    }
    compatibility.reason = join_reasons(assessment.reasons);
    compatibility_store_.updateBehaviorContext(compatibility);
}

}  // namespace edgefocus
