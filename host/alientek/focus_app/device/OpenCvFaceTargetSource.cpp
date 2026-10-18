#include "OpenCvFaceTargetSource.h"

#include "app/Logger.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <utility>

#ifdef EDGEFOCUS_WITH_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace edgefocus {

namespace {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::string getenvString(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
}

std::vector<int> parseCameraCandidates(const std::string& text) {
    std::vector<int> candidates;
    if (text.empty()) {
        for (int i = 0; i <= 9; ++i) candidates.push_back(i);
        return candidates;
    }

    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try {
            candidates.push_back(std::stoi(token));
        } catch (...) {
            LOGW("OpenCvFaceTargetSource", "Ignoring invalid camera candidate: %s", token.c_str());
        }
    }
    if (candidates.empty()) {
        for (int i = 0; i <= 9; ++i) candidates.push_back(i);
    }
    return candidates;
}

std::string resolveModelPath(const std::string& configured_path) {
    const std::string override = getenvString("EDGEFOCUS_GIMBAL_FACE_MODEL");
    if (!override.empty()) return override;
    return configured_path;
}

} // namespace

#ifdef EDGEFOCUS_WITH_OPENCV
struct OpenCvFaceTargetSource::Impl {
    cv::VideoCapture capture;
    cv::Ptr<cv::FaceDetectorYN> detector;
    int active_camera_index = -1;
};
#else
struct OpenCvFaceTargetSource::Impl {};
#endif

OpenCvFaceTargetSource::OpenCvFaceTargetSource()
    : impl_(std::make_unique<Impl>()) {}

OpenCvFaceTargetSource::~OpenCvFaceTargetSource() {
    shutdown();
}

GimbalFaceTarget OpenCvFaceTargetSource::targetFromFaceRect(int frame_width,
                                                            int frame_height,
                                                            float x,
                                                            float y,
                                                            float width,
                                                            float height,
                                                            float confidence) {
    GimbalFaceTarget target;
    if (frame_width <= 0 || frame_height <= 0 || width <= 0.0f || height <= 0.0f) {
        return target;
    }
    target.valid = true;
    target.center_x = clamp01((x + width * 0.5f) / static_cast<float>(frame_width));
    target.center_y = clamp01((y + height * 0.5f) / static_cast<float>(frame_height));
    target.width = clamp01(width / static_cast<float>(frame_width));
    target.height = clamp01(height / static_cast<float>(frame_height));
    target.confidence = clamp01(confidence);
    return target;
}

bool OpenCvFaceTargetSource::init(OpenCvFaceTargetSourceConfig config) {
    config_ = std::move(config);
    if (config_.camera_candidates.empty()) {
        config_.camera_candidates = parseCameraCandidates(getenvString("EDGEFOCUS_CAMERA_CANDIDATES"));
    }
    config_.model_path = resolveModelPath(config_.model_path);

#ifdef EDGEFOCUS_WITH_OPENCV
    if (!std::filesystem::exists(config_.model_path)) {
        last_error_ = "face model not found: " + config_.model_path;
        ready_ = false;
        return false;
    }

    impl_->detector = cv::FaceDetectorYN::create(
        config_.model_path,
        "",
        cv::Size(config_.input_width, config_.input_height),
        config_.score_threshold,
        config_.nms_threshold,
        config_.top_k);
    if (impl_->detector.empty()) {
        last_error_ = "failed to create FaceDetectorYN";
        ready_ = false;
        return false;
    }

    ready_ = true;
    last_error_.clear();
    return true;
#else
    last_error_ = "OpenCV is not compiled";
    ready_ = false;
    return false;
#endif
}

bool OpenCvFaceTargetSource::ready() const {
    return ready_;
}

const std::string& OpenCvFaceTargetSource::lastError() const {
    return last_error_;
}

void OpenCvFaceTargetSource::releaseCamera() {
#ifdef EDGEFOCUS_WITH_OPENCV
    if (impl_->capture.isOpened()) {
        impl_->capture.release();
        LOGI("OpenCvFaceTargetSource", "Released camera index %d", impl_->active_camera_index);
    }
    impl_->active_camera_index = -1;
#endif
}

void OpenCvFaceTargetSource::shutdown() {
    releaseCamera();
#ifdef EDGEFOCUS_WITH_OPENCV
    impl_->detector.release();
#endif
    ready_ = false;
}

bool OpenCvFaceTargetSource::openCameraIfNeeded() {
#ifdef EDGEFOCUS_WITH_OPENCV
    if (impl_->capture.isOpened()) return true;
    for (int index : config_.camera_candidates) {
        cv::VideoCapture candidate(index);
        if (!candidate.isOpened()) {
            continue;
        }
        candidate.set(cv::CAP_PROP_FRAME_WIDTH, config_.camera_width);
        candidate.set(cv::CAP_PROP_FRAME_HEIGHT, config_.camera_height);
        cv::Mat probe;
        if (!candidate.read(probe) || probe.empty()) {
            candidate.release();
            continue;
        }
        impl_->capture = std::move(candidate);
        impl_->active_camera_index = index;
        LOGI("OpenCvFaceTargetSource", "Opened camera index %d for gimbal face tracking", index);
        return true;
    }
    last_error_ = "no camera available for gimbal face tracking";
    return false;
#else
    return false;
#endif
}

GimbalFaceTarget OpenCvFaceTargetSource::readTarget(int64_t now_ms) {
    GimbalFaceTarget empty;
    if (!ready_) return empty;

#ifdef EDGEFOCUS_WITH_OPENCV
    if (!openCameraIfNeeded()) return empty;

    cv::Mat frame;
    if (!impl_->capture.read(frame) || frame.empty()) {
        last_error_ = "camera read failed";
        releaseCamera();
        return empty;
    }
    return detectFrame(frame, now_ms);
#else
    (void)now_ms;
    return empty;
#endif
}

#ifdef EDGEFOCUS_WITH_OPENCV
GimbalFaceTarget OpenCvFaceTargetSource::detectFrame(const cv::Mat& frame,
                                                     int64_t now_ms) {
    if (frame.empty() || impl_->detector.empty()) {
        return {};
    }

    cv::Mat bgr;
    if (frame.channels() == 1) {
        cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);
    } else {
        bgr = frame;
    }

    impl_->detector->setInputSize(cv::Size(bgr.cols, bgr.rows));
    cv::Mat detections;
    const int status = impl_->detector->detect(bgr, detections);
    if (status != 1 || detections.empty()) {
        return {};
    }

    int best_row = -1;
    float best_score = -1.0f;
    float best_area = 0.0f;
    for (int row = 0; row < detections.rows; ++row) {
        const float* data = detections.ptr<float>(row);
        const float score = data[14];
        const float area = std::max(0.0f, data[2]) * std::max(0.0f, data[3]);
        if (score > best_score || (score == best_score && area > best_area)) {
            best_score = score;
            best_area = area;
            best_row = row;
        }
    }
    if (best_row < 0 || best_score < config_.score_threshold) {
        return {};
    }

    const float* face = detections.ptr<float>(best_row);
    GimbalFaceTarget target = targetFromFaceRect(
        bgr.cols,
        bgr.rows,
        face[0],
        face[1],
        face[2],
        face[3],
        best_score);
    target.timestamp_ms = now_ms;
    return target;
}
#endif

} // namespace edgefocus
