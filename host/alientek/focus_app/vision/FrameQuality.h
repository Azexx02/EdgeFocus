#pragma once

#include "camera/CameraFrame.h"
#include "vision/VisionResult.h"

namespace edgefocus {

struct FrameQualityMetrics {
    float mean_luma = 0.0f;
    float contrast = 0.0f;
    float edge_score = 0.0f;
};

class FrameQualityEvaluator {
public:
    FrameQuality evaluate(const CameraFrame& frame, FrameQualityMetrics* metrics = nullptr) const;
};

} // namespace edgefocus
