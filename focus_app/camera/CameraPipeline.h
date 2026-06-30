#pragma once
#include "app/BoundedQueue.h"
#include "app/Module.h"
#include "app/SystemStateStore.h"
#include "camera/CameraFrame.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace edgefocus {

class AppContext;

/// Camera capture module. Acquires frames and publishes them to a bounded queue.
/// Session-aware: camera only opens when session.active is true.
class CameraPipeline : public Module {
public:
    using Queue = BoundedQueue<CameraFrame>;

    CameraPipeline(Queue& output_queue, SystemStateStore& store);
    ~CameraPipeline() override;

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    int64_t capturedFrames() const { return captured_frames_; }
    int64_t droppedFrames() const { return dropped_frames_; }

private:
    bool openCamera();
    void closeCamera();
    bool readFrame(CameraFrame* frame);
    bool cameraDisabled() const;
    std::vector<int> cameraCandidates() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    Queue& output_queue_;
    SystemStateStore& store_;
    bool camera_opened_ = false;
    int64_t next_open_attempt_ms_ = 0;
    int64_t captured_frames_ = 0;
    int64_t dropped_frames_ = 0;
};

} // namespace edgefocus
