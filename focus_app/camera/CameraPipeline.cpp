#include "CameraPipeline.h"
#include "app/AppContext.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <sstream>
#include <thread>

#ifdef EDGEFOCUS_WITH_OPENCV
#include <opencv2/videoio.hpp>
#endif

namespace edgefocus {

namespace {

constexpr int kPreviewWidth = 40;
constexpr int kPreviewHeight = 30;
constexpr int64_t kPreviewIntervalMs = 1200;

int64_t nowMs() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::string base64Encode(const std::vector<uint8_t>& bytes) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t b0 = bytes[i];
        const uint32_t b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const uint32_t b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kTable[(triple >> 18) & 0x3f]);
        out.push_back(kTable[(triple >> 12) & 0x3f]);
        out.push_back(i + 1 < bytes.size() ? kTable[(triple >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < bytes.size() ? kTable[triple & 0x3f] : '=');
    }
    return out;
}

uint8_t grayAt(const CameraFrame& frame, int x, int y) {
    const size_t index = static_cast<size_t>((y * frame.width + x) * frame.channels);
    if (index >= frame.data.size()) return 0;
    if (frame.channels == 1) return frame.data[index];

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (frame.format == PixelFormat::RGB24) {
        r = frame.data[index];
        g = index + 1 < frame.data.size() ? frame.data[index + 1] : r;
        b = index + 2 < frame.data.size() ? frame.data[index + 2] : r;
    } else {
        b = frame.data[index];
        g = index + 1 < frame.data.size() ? frame.data[index + 1] : b;
        r = index + 2 < frame.data.size() ? frame.data[index + 2] : b;
    }
    return static_cast<uint8_t>((static_cast<int>(r) * 30 +
                                 static_cast<int>(g) * 59 +
                                 static_cast<int>(b) * 11) / 100);
}

CameraPreviewImage makePreviewImage(const CameraFrame& frame, int64_t sequence) {
    CameraPreviewImage preview;
    if (!frame.valid()) return preview;

    preview.available = true;
    preview.timestamp_ms = frame.timestamp_ms;
    preview.sequence = sequence;
    preview.width = kPreviewWidth;
    preview.height = kPreviewHeight;
    preview.pixel_format = "GRAY8";
    preview.encoding = "base64";
    preview.source = frame.source;

    std::vector<uint8_t> gray;
    gray.reserve(static_cast<size_t>(kPreviewWidth * kPreviewHeight));
    for (int y = 0; y < kPreviewHeight; ++y) {
        const int src_y = std::min(frame.height - 1, y * frame.height / kPreviewHeight);
        for (int x = 0; x < kPreviewWidth; ++x) {
            const int src_x = std::min(frame.width - 1, x * frame.width / kPreviewWidth);
            gray.push_back(grayAt(frame, src_x, src_y));
        }
    }
    preview.data_base64 = base64Encode(gray);
    return preview;
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

bool envEnabled(const char* value) {
    if (!value) return false;
    std::string text(value);
    return text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON";
}

std::string getenvString(const char* key) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, key) != 0 || !value) return "";
    std::unique_ptr<char, decltype(&std::free)> holder(value, &std::free);
    return std::string(value);
#else
    const char* value = std::getenv(key);
    return value ? std::string(value) : std::string();
#endif
}

} // namespace

struct CameraPipeline::Impl {
#ifdef EDGEFOCUS_WITH_OPENCV
    cv::VideoCapture capture;
#endif
    int active_index = -1;
};

CameraPipeline::CameraPipeline(Queue& output_queue, SystemStateStore& store)
    : Module("CameraPipeline"), impl_(new Impl()), output_queue_(output_queue), store_(store) {}

CameraPipeline::~CameraPipeline() = default;

bool CameraPipeline::init(AppContext& /*ctx*/) {
    captured_frames_ = 0;
    dropped_frames_ = 0;
    last_preview_ms_ = 0;
    preview_sequence_ = 0;
    next_open_attempt_ms_ = 0;
    // Camera opens on-demand when session starts
    if (cameraDisabled()) {
        DeviceStatus status = store_.snapshot().device;
        status.camera_ok = false;
        store_.updateDeviceStatus(status);
        LOGW("CameraPipeline", "Initialized with camera disabled by EDGEFOCUS_CAMERA_DISABLED");
    } else {
        LOGI("CameraPipeline", "Initialized (camera will open on session start)");
    }
    return true;
}

bool CameraPipeline::cameraDisabled() const {
    const std::string disabled = getenvString("EDGEFOCUS_CAMERA_DISABLED");
    return envEnabled(disabled.empty() ? nullptr : disabled.c_str());
}

std::vector<int> CameraPipeline::cameraCandidates() const {
    const std::string candidates = getenvString("EDGEFOCUS_CAMERA_CANDIDATES");
    return parseCandidates(candidates.empty() ? nullptr : candidates.c_str());
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

void CameraPipeline::runOnce(AppContext& ctx) {
    SystemSnapshot snap = store_.snapshot();

    // --- IDLE: session not active ---
    if (!snap.session.active) {
        if (camera_opened_) {
            closeCamera();
            camera_opened_ = false;
            LOGI("CameraPipeline", "Session ended, camera released");
        }
        // Block until session becomes active (with timeout for shutdown check)
        std::unique_lock<std::mutex> lock(store_.mutexForCV());
        store_.waitForSessionChange(lock, 1000);
        return;
    }

    // --- ACTIVE: session is running ---
    if (cameraDisabled()) {
        if (camera_opened_) {
            closeCamera();
            camera_opened_ = false;
        }
        DeviceStatus status = snap.device;
        if (status.camera_ok) {
            status.camera_ok = false;
            store_.updateDeviceStatus(status);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return;
    }

    // --- ACTIVE but paused: freeze capture without cycling the OpenCV backend.
    // Some board GStreamer/V4L2 pipelines are fragile when repeatedly released
    // and reopened; pausing only needs to stop frame production.
    if (snap.session.paused) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return;
    }

    if (!camera_opened_) {
        const int64_t now = nowMs();
        if (now < next_open_attempt_ms_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return;
        }
        if (!openCamera()) {
            DeviceStatus status = snap.device;
            status.camera_ok = false;
            store_.updateDeviceStatus(status);
            next_open_attempt_ms_ = nowMs() + 15000;
            LOGW("CameraPipeline", "Cannot open camera, retrying later");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return;
        }
        DeviceStatus status = snap.device;
        status.camera_ok = true;
        store_.updateDeviceStatus(status);
        next_open_attempt_ms_ = 0;
        camera_opened_ = true;
    }

    CameraFrame frame;
    if (!readFrame(&frame)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    if (frame.timestamp_ms - last_preview_ms_ >= kPreviewIntervalMs) {
        last_preview_ms_ = frame.timestamp_ms;
        store_.updateCameraPreviewImage(makePreviewImage(frame, ++preview_sequence_));
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

void CameraPipeline::closeCamera() {
#ifdef EDGEFOCUS_WITH_OPENCV
    if (impl_->capture.isOpened()) {
        impl_->capture.release();
    }
#endif
    impl_->active_index = -1;
}

void CameraPipeline::shutdown(AppContext& /*ctx*/) {
    closeCamera();
    LOGI("CameraPipeline", "Shutdown captured=%lld dropped_old_frames=%lld",
         (long long)captured_frames_, (long long)dropped_frames_);
}

} // namespace edgefocus
