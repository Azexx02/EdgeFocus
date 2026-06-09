#pragma once

#include "app/BoundedQueue.h"
#include "camera/CameraFrame.h"
#include "vision/FrameQuality.h"
#include "vision/ModelAdapter.h"
#include "vision/ModelRegistry.h"
#include "vision/PoseModelAdapter.h"
#include "vision/VisionResult.h"

#include <memory>
#include <string>

namespace edgefocus {

enum class VisionRuntimeMode : uint8_t {
    AUTO = 0,
    MOCK = 1,
    REAL = 2,
    FALLBACK = 3
};

class InferenceScheduler {
public:
    using FrameQueue = BoundedQueue<CameraFrame>;
    using ResultQueue = BoundedQueue<VisionResult>;

    InferenceScheduler(FrameQueue& frame_queue, ResultQueue& result_queue);

    bool init(VisionRuntimeMode mode, const std::string& registry_path);
    bool runOnce();
    void shutdown();
    int64_t processedFrames() const { return processed_frames_; }
    const char* activeAdapterName() const;

private:
    VisionResult buildResult(const CameraFrame& frame, const FrameQuality& quality, const ModelInference& inference) const;
    ModelInference runBestAdapter(const CameraFrame& frame, const FrameQualityMetrics& metrics);

    FrameQueue& frame_queue_;
    ResultQueue& result_queue_;
    FrameQualityEvaluator quality_evaluator_;
    ModelRegistry registry_;
    std::unique_ptr<ModelAdapter> real_adapter_;
    std::unique_ptr<ModelAdapter> fallback_adapter_;
    VisionRuntimeMode mode_ = VisionRuntimeMode::AUTO;
    int64_t processed_frames_ = 0;
    int64_t dropped_results_ = 0;
};

VisionRuntimeMode parseVisionRuntimeMode(const char* value);

} // namespace edgefocus
