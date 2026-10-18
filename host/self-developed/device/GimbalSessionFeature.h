#pragma once

#include "device/GimbalPrecheckController.h"
#include "device/GimbalVisualTracker.h"
#include "session/ISessionFeature.h"
#include "session/SessionSnapshotPublisher.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace edgefocus::gimbal {

class GimbalSessionFeature final : public session::ISessionFeature {
public:
    using Clock = std::function<std::int64_t()>;
    using Sleeper = std::function<void(std::chrono::milliseconds)>;

    GimbalSessionFeature(GimbalPrecheckController& controller,
                         GimbalVisualTracker& tracker,
                         session::SessionSnapshotPublisher& snapshots,
                         Clock clock = {}, Sleeper sleeper = {});
    ~GimbalSessionFeature() override;

    const char* name() const noexcept override { return "gimbal_precheck"; }
    session::PrepareResult prepare(const session::SessionContext& context) override;
    void enterPhase(session::Phase phase) override;
    void stop(session::StopReason reason) noexcept override;
    session::FeatureHealth health() const override;

private:
    static std::int64_t defaultClockMs();
    void workerLoop();
    void publish(std::int64_t now_ms);
    void setHealth(bool available, bool degraded, std::string status, std::string detail = {});

    GimbalPrecheckController& controller_;
    GimbalVisualTracker& tracker_;
    session::SessionSnapshotPublisher& snapshots_;
    Clock clock_;
    Sleeper sleeper_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<session::Phase> phase_{session::Phase::Idle};
    std::atomic<std::int64_t> tracking_accept_after_ms_{0};
    mutable std::mutex health_mutex_;
    session::FeatureHealth health_;
};

}  // namespace edgefocus::gimbal
