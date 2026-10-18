#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef EDGEFOCUS_WITH_OPENCV
namespace cv {
class Mat;
}
#endif

namespace edgefocus {

struct GimbalFaceTarget {
    bool valid = false;
    int64_t timestamp_ms = 0;
    float center_x = 0.5f;
    float center_y = 0.5f;
    float width = 0.0f;
    float height = 0.0f;
    float confidence = 0.0f;
};

struct OpenCvFaceTargetSourceConfig {
    std::string model_path = "config/models/face_detection_yunet_2023mar.onnx";
    std::vector<int> camera_candidates;
    int camera_width = 640;
    int camera_height = 480;
    int input_width = 320;
    int input_height = 320;
    float score_threshold = 0.72f;
    float nms_threshold = 0.30f;
    int top_k = 5000;
};

class OpenCvFaceTargetSource {
public:
    OpenCvFaceTargetSource();
    ~OpenCvFaceTargetSource();

    OpenCvFaceTargetSource(const OpenCvFaceTargetSource&) = delete;
    OpenCvFaceTargetSource& operator=(const OpenCvFaceTargetSource&) = delete;

    bool init(OpenCvFaceTargetSourceConfig config = {});
    GimbalFaceTarget readTarget(int64_t now_ms);
    void releaseCamera();
    void shutdown();

    bool ready() const;
    const std::string& lastError() const;

    static GimbalFaceTarget targetFromFaceRect(int frame_width, int frame_height,
                                               float x, float y, float width,
                                               float height, float confidence);

private:
    bool openCameraIfNeeded();
#ifdef EDGEFOCUS_WITH_OPENCV
    GimbalFaceTarget detectFrame(const cv::Mat& frame, int64_t now_ms);
#endif

    struct Impl;
    std::unique_ptr<Impl> impl_;
    OpenCvFaceTargetSourceConfig config_;
    bool ready_ = false;
    std::string last_error_;
};

} // namespace edgefocus
