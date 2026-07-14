#pragma once

#include "app/BoundedQueue.h"
#include "behavior/DistractionAssessmentEngine.h"
#include "behavior/PhoneFactBuffer.h"
#include "camera/CameraFrame.h"
#include "session/ISessionFeature.h"
#include "session/SessionSnapshotPublisher.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace edgefocus {

class SystemStateStore;

class DistractionPipelineFeature final : public session::ISessionFeature {
public:
    using FrameQueue = BoundedQueue<CameraFrame>;

    DistractionPipelineFeature(
        FrameQueue& frames,
        PhoneFactBuffer& phone_facts,
        session::SessionSnapshotPublisher& snapshots,
        SystemStateStore& compatibility_store,
        std::string pose_model_path = {},
        std::string prone_model_path = {});
    ~DistractionPipelineFeature() override;

    const char* name() const noexcept override { return "distraction_pipeline"; }
    session::PrepareResult prepare(const session::SessionContext& context) override;
    void enterPhase(session::Phase phase) override;
    void stop(session::StopReason reason) noexcept override;
    session::FeatureHealth health() const override;

private:
    struct Impl;

    static std::string configuredPath(const char* env_name, const char* fallback);
    static std::int64_t monotonicMs();
    void workerLoop();
    void processFrame(const CameraFrame& frame, session::Phase phase);
    void setHealth(bool available, bool degraded, std::string status, std::string detail = {});
    void publishCompatibility(
        const session::DistractionAssessment& assessment,
        const session::PhoneSnapshot& phone);

    FrameQueue& frames_;
    PhoneFactBuffer& phone_facts_;
    session::SessionSnapshotPublisher& snapshots_;
    SystemStateStore& compatibility_store_;
    std::string pose_model_path_;
    std::string prone_model_path_;

    session::SessionContext context_;
    DistractionAssessmentEngine assessment_engine_;
    std::unique_ptr<Impl> impl_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<session::Phase> phase_{session::Phase::Idle};
    mutable std::mutex processing_mutex_;
    mutable std::mutex health_mutex_;
    session::FeatureHealth health_;
    std::int64_t last_paused_inference_ms_{};
    std::int64_t last_inference_frame_ms_{};
    double inference_fps_{};
};

}  // namespace edgefocus
