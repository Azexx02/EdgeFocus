#pragma once

#include "app/Module.h"
#include "device/GimbalController.h"
#include "session/SessionDomain.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

namespace edgefocus {

class SystemStateStore;

class GimbalMonitor : public Module {
public:
    explicit GimbalMonitor(SystemStateStore& store);

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;
    GimbalCommandResult executeBusinessCommand(const std::string& command, int timeout_ms);
    void beginSession();
    void enterSessionPhase(session::Phase phase);
    void endSession() noexcept;

private:
    bool enabled() const;
    void publishSnapshot();
    void runAutoInitializationLocked();

    SystemStateStore& store_;
    GimbalController controller_;
    std::mutex controller_mutex_;
    std::chrono::steady_clock::time_point last_poll_ =
        std::chrono::steady_clock::time_point{};
    bool auto_init_attempted_ = false;
    bool auto_init_running_ = false;
    bool auto_initialized_ = false;
    std::atomic<bool> session_active_{false};
    std::atomic<session::Phase> session_phase_{session::Phase::Idle};
    GimbalCommandResult last_command_result_;
    static constexpr int kPollIntervalMs = 500;
};

}  // namespace edgefocus
