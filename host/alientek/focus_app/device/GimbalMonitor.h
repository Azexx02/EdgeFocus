#pragma once

#include "app/Module.h"
#include "device/GimbalController.h"
#include "device/GimbalFaceTracker.h"
#include "device/OpenCvFaceTargetSource.h"
#include <chrono>
#include <mutex>
#include <string>

namespace edgefocus {

class SystemStateStore;
struct SystemSnapshot;

class GimbalMonitor : public Module {
public:
    explicit GimbalMonitor(SystemStateStore& store);

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;
    GimbalCommandResult executeBusinessCommand(const std::string& command, int timeout_ms);

private:
    bool enabled() const;
    void publishSnapshot();
    void runAutoInitializationLocked();
    void runFaceTrackingLocked();
    bool trackingAllowed(const SystemSnapshot& snap,
                         const GimbalSnapshot& gs) const;

    SystemStateStore& store_;
    GimbalController controller_;
    GimbalFaceTracker face_tracker_;
    OpenCvFaceTargetSource face_target_source_;
    std::mutex controller_mutex_;
    std::chrono::steady_clock::time_point last_poll_ =
        std::chrono::steady_clock::time_point{};
    bool auto_init_attempted_ = false;
    bool auto_init_running_ = false;
    bool auto_initialized_ = false;
    GimbalCommandResult last_command_result_;
    static constexpr int kPollIntervalMs = 500;
};

}  // namespace edgefocus
