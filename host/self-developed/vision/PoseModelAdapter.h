#pragma once

#include "vision/ModelAdapter.h"

#include <memory>

namespace edgefocus {

class CpuFallbackPoseAdapter : public ModelAdapter {
public:
    bool init(const ModelPackage& package) override;
    ModelInference infer(const CameraFrame& frame, const FrameQualityMetrics& metrics) override;
    const char* name() const override { return "cpu_fallback_pose"; }
    bool loaded() const override { return loaded_; }

private:
    bool loaded_ = false;
};

class StaiMpuPoseAdapter : public ModelAdapter {
public:
    StaiMpuPoseAdapter();
    ~StaiMpuPoseAdapter() override;

    bool init(const ModelPackage& package) override;
    ModelInference infer(const CameraFrame& frame, const FrameQualityMetrics& metrics) override;
    const char* name() const override { return "stai_mpu_pose"; }
    bool loaded() const override { return loaded_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
};

} // namespace edgefocus
