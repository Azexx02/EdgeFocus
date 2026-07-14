#include "device/GimbalSessionFeature.h"

#include "app/SystemStateStore.h"
#include "device/GimbalMonitor.h"
#include "session/SessionSnapshotPublisher.h"

#include <chrono>

namespace edgefocus {
namespace {
std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

GimbalSessionFeature::GimbalSessionFeature(
    GimbalMonitor& monitor,
    SystemStateStore& store,
    session::SessionSnapshotPublisher& snapshots)
    : monitor_(monitor), store_(store), snapshots_(snapshots) {}

session::PrepareResult GimbalSessionFeature::prepare(const session::SessionContext&) {
    monitor_.beginSession();
    active_ = true;
    const auto device = store_.snapshot().device;
    health_ = {device.gimbal_online,
               !device.gimbal_online,
               device.gimbal_online ? "ready" : "degraded_fixed_camera",
               device.gimbal_online ? "" : "gimbal offline; inference continues",
               nowMs()};
    publish(session::Phase::Precheck);
    return device.gimbal_online
               ? session::PrepareResult::success()
               : session::PrepareResult::degraded_success(health_.detail);
}

void GimbalSessionFeature::enterPhase(session::Phase phase) {
    if (!active_) return;
    monitor_.enterSessionPhase(phase);
    publish(phase);
}

void GimbalSessionFeature::stop(session::StopReason) noexcept {
    if (!active_) return;
    monitor_.endSession();
    active_ = false;
    health_ = {false, false, "stopped", {}, nowMs()};
    publish(session::Phase::Idle);
}

session::FeatureHealth GimbalSessionFeature::health() const {
    return health_;
}

void GimbalSessionFeature::publish(session::Phase phase) {
    const auto device = store_.snapshot().device;
    session::GimbalSnapshot value;
    value.state = !active_ ? "idle"
                          : (device.gimbal_online ? session::phase_name(phase)
                                                  : "degraded_fixed_camera");
    value.health = health_;
    value.angle_x = device.gimbal_angle_x;
    value.angle_y = device.gimbal_angle_y;
    // OpenCV tracking is a later optional expert; never report it active until
    // a tracker has actually acquired the session subject.
    value.tracking = false;
    snapshots_.updateGimbal(value, nowMs());
}

}  // namespace edgefocus
