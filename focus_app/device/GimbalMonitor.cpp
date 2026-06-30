#include "GimbalMonitor.h"

#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "app/Logger.h"

#include <thread>

namespace edgefocus {

GimbalMonitor::GimbalMonitor(SystemStateStore& store)
    : Module("GimbalMonitor"), store_(store) {}

bool GimbalMonitor::init(AppContext& /*ctx*/) {
    LOGI("GimbalMonitor", "Init");
    publishSnapshot();
    return true;
}

void GimbalMonitor::publishSnapshot() {
    const GimbalSnapshot& gs = controller_.status();
    DeviceStatus status = store_.snapshot().device;
    status.gimbal_connected = gs.connected;
    status.gimbal_online = gs.online;
    status.gimbal_boost = gs.boost;
    status.gimbal_motor = gs.motor;
    status.gimbal_safe = gs.safe;
    status.gimbal_en = gs.en;
    status.gimbal_ix = gs.ix;
    status.gimbal_iy = gs.iy;
    status.gimbal_angle_x = gs.angle_x;
    status.gimbal_angle_y = gs.angle_y;
    status.gimbal_last_seen_ms = gs.last_seen_ms;
    status.gimbal_last_error = gs.last_error;
    store_.updateDeviceStatus(status);
}

void GimbalMonitor::runOnce(AppContext& ctx) {
    auto now = std::chrono::steady_clock::now();
    if (last_poll_ == std::chrono::steady_clock::time_point{} ||
        now - last_poll_ >= std::chrono::milliseconds(kPollIntervalMs)) {
        last_poll_ = now;
        controller_.pollOnce();
        publishSnapshot();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

void GimbalMonitor::shutdown(AppContext& /*ctx*/) {
    controller_.shutdown();
    LOGI("GimbalMonitor", "Shutdown");
}

}  // namespace edgefocus
