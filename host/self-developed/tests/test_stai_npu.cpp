#include <iostream>
#include <chrono>
#include <string>
#include "vision/PoseModelAdapter.h"
#include "vision/FrameQuality.h"

#ifdef EDGEFOCUS_WITH_OPENCV
#include <opencv2/opencv.hpp>
#endif

using namespace edgefocus;

int main(int argc, char** argv) {
    std::cout << "=== EdgeFocus NPU Test ===" << std::endl;

#ifndef EDGEFOCUS_WITH_OPENCV
    std::cerr << "ERROR: OpenCV is required for this test to load images!" << std::endl;
    return 1;
#endif

#ifndef EDGEFOCUS_WITH_STAI_MPU
    std::cerr << "ERROR: STAI MPU is not enabled!" << std::endl;
    return 1;
#endif

    std::string model_path = "/usr/local/x-linux-ai/pose-estimation/models/yolov8n_pose/yolov8n_256_quant_pt_uf_pose_coco-st.nb";
    if (argc > 1) {
        model_path = argv[1];
    }

    std::cout << "Loading model: " << model_path << std::endl;
    ModelPackage pkg;
    pkg.model_path = model_path;
    pkg.confidence_threshold = 0.5f;
    pkg.iou_threshold = 0.45f;

    StaiMpuPoseAdapter adapter;
    bool initOk = adapter.init(pkg);
    if (!initOk) {
        std::cerr << "Failed to init StaiMpuPoseAdapter!" << std::endl;
        return 1;
    }
    std::cout << "Model initialized successfully." << std::endl;

    std::string image_path = (argc > 2) ? argv[2] : "";
    cv::Mat img;
    if (!image_path.empty()) {
        img = cv::imread(image_path);
    }
    if (img.empty()) {
        std::cout << "No valid image provided, creating a 640x480 dummy frame (black)." << std::endl;
        img = cv::Mat::zeros(480, 640, CV_8UC3);
    } else {
        std::cout << "Loaded image: " << image_path << " (" << img.cols << "x" << img.rows << ")" << std::endl;
    }

    CameraFrame frame;
    frame.width = img.cols;
    frame.height = img.rows;
    frame.channels = img.channels();
    frame.timestamp_ms = 1000;
    frame.data.assign(img.data, img.data + img.total() * img.elemSize());

    FrameQualityMetrics metrics; // dummy

    // Warmup
    std::cout << "Running warmup inference..." << std::endl;
    adapter.infer(frame, metrics);

    // Test
    std::cout << "Running actual inference..." << std::endl;
    auto start = std::chrono::steady_clock::now();
    ModelInference result = adapter.infer(frame, metrics);
    auto end = std::chrono::steady_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "=== Inference Results ===" << std::endl;
    std::cout << "Total wrapper call time : " << dur << " ms" << std::endl;
    std::cout << "NPU API Reported time   : " << result.inference_time_ms << " ms" << std::endl;
    std::cout << "Model Ran               : " << (result.ran_model ? "Yes" : "No") << std::endl;
    std::cout << "Person Detected         : " << (result.person_detected ? "Yes" : "No") << std::endl;
    if (result.person_detected) {
        std::cout << "Person Confidence       : " << result.person_confidence << std::endl;
        std::cout << "Head Down Score         : " << result.head_down_score << std::endl;
        std::cout << "Multiple People         : " << (result.multiple_people ? "Yes" : "No") << std::endl;
        std::cout << "Pitch                   : " << result.head_pose.value.pitch << std::endl;
    } else {
        std::cout << "Reason                  : " << static_cast<int>(result.reason_code) << std::endl;
    }
    std::cout << "Debug Note              : " << result.debug_note << std::endl;
    std::cout << "=========================" << std::endl;

    return 0;
}
