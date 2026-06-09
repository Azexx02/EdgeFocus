#include "CameraPipeline.h"
#include "app/AppContext.h"

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

#ifdef EDGEFOCUS_WITH_OPENCV
#include <opencv2/videoio.hpp>
#endif

namespace edgefocus {

namespace {

int64_t nowMs() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::vector<int> parseCandidates(const char* value) {
    std::vector<int> candidates;
    if (!value || !*value) {
        for (int i = 0; i <= 9; ++i) candidates.push_back(i);
        return candidates;
    }
    std::stringstream ss(value);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try {
            candidates.push_back(std::stoi(token));
        } catch (...) {
            LOGW("CameraPipeline", "Ignoring invalid camera candidate: %s", token.c_str());
        }
    }
    if (candidates.empty()) {
        for (int i = 0; i <= 9; ++i) candidates.push_back(i);
    }
    return candidates;
}

} // namespace

struct CameraPipeline::Impl {
#ifdef EDGEFOCUS_WITH_OPENCV
    cv::VideoCapture capture;
#endif
    int active_index = -1;
};

CameraPipeline::CameraPipeline(Queue& output_queue)
    : Module("CameraPipeline"), impl_(new Impl()), output_queue_(output_queue) {}

CameraPipeline::~CameraPipeline() = default;

bool CameraPipeline::init(AppContext& /*ctx*/) {
    captured_frames_ = 0;
    dropped_frames_ = 0;
    if (!openCamera()) {
        LOGW("CameraPipeline", "No camera opened; vision can still run in mock mode or wait for frames");
    }
    return true;
}

std::vector<int> CameraPipeline::cameraCandidates() const {
    return parseCandidates(std::getenv("EDGEFOCUS_CAMERA_CANDIDATES"));
}

bool CameraPipeline::openCamera() {
#ifdef EDGEFOCUS_WITH_OPENCV
    const std::vector<int> candidates = cameraCandidates();
    for (int index : candidates) {
        cv::VideoCapture cap(index);
        if (!cap.isOpened()) {
            continue;
        }
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        impl_->capture = std::move(cap);
        impl_->active_index = index;
        CameraFrame probe;
        if (readFrame(&probe)) {
            LOGI("CameraPipeline", "Opened camera index %d (%dx%d)", index, probe.width, probe.height);
            return true;
        }
        impl_->capture.release();
        impl_->active_index = -1;
    }
    LOGW("CameraPipeline", "OpenCV could not open any camera candidate");
    return false;
#else
    LOGW("CameraPipeline", "OpenCV capture not compiled; use mock mode or build with EDGEFOCUS_WITH_OPENCV=ON");
    return false;
#endif
}

bool CameraPipeline::readFrame(CameraFrame* frame) {
#ifdef EDGEFOCUS_WITH_OPENCV
    if (!frame || !impl_->capture.isOpened()) return false;
    cv::Mat mat;
    if (!impl_->capture.read(mat) || mat.empty()) {
        return false;
    }
    frame->timestamp_ms = nowMs();
    frame->width = mat.cols;
    frame->height = mat.rows;
    frame->channels = mat.channels();
    frame->format = frame->channels == 1 ? PixelFormat::GRAY8 : PixelFormat::BGR24;
    frame->source = "camera_index_" + std::to_string(impl_->active_index);
    const size_t bytes = mat.total() * mat.elemSize();
    frame->data.assign(mat.data, mat.data + bytes);
    return frame->valid();
#else
    (void)frame;
    return false;
#endif
}

void CameraPipeline::runOnce(AppContext& /*ctx*/) {
    CameraFrame frame;
    if (!readFrame(&frame)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    if (!output_queue_.push(std::move(frame))) {
        ++dropped_frames_;
    }
    ++captured_frames_;
    if (captured_frames_ % 60 == 0) {
        LOGI("CameraPipeline", "Captured frames=%lld dropped_old_frames=%lld",
             (long long)captured_frames_, (long long)dropped_frames_);
    }
}

void CameraPipeline::shutdown(AppContext& /*ctx*/) {
#ifdef EDGEFOCUS_WITH_OPENCV
    if (impl_->capture.isOpened()) {
        impl_->capture.release();
    }
#endif
    LOGI("CameraPipeline", "Shutdown captured=%lld dropped_old_frames=%lld",
         (long long)captured_frames_, (long long)dropped_frames_);
}

} // namespace edgefocus
