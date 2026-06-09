#pragma once
#include "app/BoundedQueue.h"
#include "app/Module.h"
#include "camera/CameraFrame.h"
#include "vision/InferenceScheduler.h"
#include "vision/VisionResult.h"

#include <cstdint>
#include <memory>

#include "vision/MockVisionSource.h"

namespace edgefocus {

class AppContext;

/// Vision AI module. Reads camera frames, runs inference, outputs VisionResult.
/// Mock mode is retained for no-camera and behavior-layer tests.
class VisionEngine : public Module {
public:
    using Queue = BoundedQueue<VisionResult>;
    using FrameQueue = BoundedQueue<CameraFrame>;

    explicit VisionEngine(Queue& output_queue);
    VisionEngine(Queue& output_queue, FrameQueue& frame_queue);
    ~VisionEngine() override;

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    void setMockScenario(MockScenario scenario);
    MockScenario mockScenario() const { return mock_source_.scenario(); }

private:
    Queue& output_queue_;
    FrameQueue* frame_queue_ = nullptr;
    std::unique_ptr<InferenceScheduler> scheduler_;
    VisionRuntimeMode runtime_mode_ = VisionRuntimeMode::AUTO;
    MockVisionSource mock_source_;
    int64_t frame_counter_ = 0;
};

} // namespace edgefocus
