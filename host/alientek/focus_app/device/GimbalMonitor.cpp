#include "GimbalMonitor.h"

#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "app/Logger.h"

#include <chrono>
#include <thread>

namespace edgefocus {

GimbalMonitor::GimbalMonitor(SystemStateStore& store)
    : Module("GimbalMonitor"), store_(store) {}

bool GimbalMonitor::enabled() const {
    return store_.snapshot().preferences.gimbal_enabled;
}

bool GimbalMonitor::trackingAllowed(const SystemSnapshot& snap,
                                    const GimbalSnapshot& gs) const {
    return snap.preferences.gimbal_enabled && gs.online && gs.safe;
}

bool GimbalMonitor::init(AppContext& /*ctx*/) {
    LOGI("GimbalMonitor", "Init");
    if (!face_target_source_.init()) {
        LOGW("GimbalMonitor", "OpenCV face tracking source inactive: %s",
             face_target_source_.lastError().c_str());
    }
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
    status.gimbal_xhome = gs.xhome;
    status.gimbal_yhome = gs.yhome;
    status.gimbal_calibrated = gs.cal;
    status.gimbal_en = gs.en;
    status.gimbal_ix = gs.ix;
    status.gimbal_iy = gs.iy;
    status.gimbal_angle_x = gs.angle_x;
    status.gimbal_angle_y = gs.angle_y;
    status.gimbal_last_seen_ms = gs.last_seen_ms;
    status.gimbal_last_error = gs.last_error;
    status.gimbal_initializing = auto_init_running_;
    status.gimbal_auto_initialized = auto_initialized_;
    status.gimbal_last_command = last_command_result_.command;
    status.gimbal_last_command_status = last_command_result_.success
                                             ? "success"
                                             : (last_command_result_.timeout ? "timeout"
                                                                             : last_command_result_.error);
    status.gimbal_last_command_response = last_command_result_.final_response.empty()
                                              ? last_command_result_.start_response
                                              : last_command_result_.final_response;
    store_.updateDeviceStatus(status);
}

void GimbalMonitor::runAutoInitializationLocked() {
    if (auto_init_attempted_) return;
    const auto& gs = controller_.status();
    if (!GimbalController::readyForAutoInitialization(gs, controller_.state())) return;

    auto_init_attempted_ = true;
    auto_init_running_ = true;
    last_command_result_ = controller_.executeBusinessCommand("CENTER X", 30000);
    if (last_command_result_.success) {
        last_command_result_ = controller_.executeBusinessCommand("DEG Y 0", 10000);
    }
    auto_initialized_ = last_command_result_.success;
    auto_init_running_ = false;
}

void GimbalMonitor::runFaceTrackingLocked() {
    const auto snap = store_.snapshot();
    const GimbalSnapshot gs = controller_.status();
    const auto now = std::chrono::steady_clock::now();
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now.time_since_epoch()).count();

    if (!trackingAllowed(snap, gs)) {
        face_tracker_.reset();
        face_target_source_.releaseCamera();
        return;
    }

    const auto target = face_target_source_.readTarget(now_ms);

    GimbalTrackingInput input;
    input.enabled = true;
    input.gimbal_ready = true;
    input.target_valid = target.valid;
    input.target_timestamp_ms = target.timestamp_ms;
    input.target_center_x = target.center_x;
    input.target_center_y = target.center_y;
    input.target_confidence = target.confidence;
    input.current_angle_x = gs.angle_x;
    input.current_angle_y = gs.angle_y;
    input.now_ms = now_ms;

    auto command = face_tracker_.update(input);
    if (!command.has_value() || auto_init_running_) return;

    auto result = controller_.sendTrackingSetXy(command->x_deg, command->y_deg);
    if (!result.success) {
        last_command_result_ = result;
    } else if (last_command_result_.command.empty() ||
               last_command_result_.error == "gimbal_disabled") {
        last_command_result_ = result;
    }
    publishSnapshot();
}

void GimbalMonitor::runOnce(AppContext& ctx) {
    (void)ctx;
    if (!enabled()) {
        face_tracker_.reset();
        face_target_source_.releaseCamera();
        DeviceStatus status = store_.snapshot().device;
        status.gimbal_connected = false;
        status.gimbal_online = false;
        status.gimbal_initializing = false;
        status.gimbal_last_error = "disabled";
        store_.updateDeviceStatus(status);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (last_poll_ == std::chrono::steady_clock::time_point{} ||
        now - last_poll_ >= std::chrono::milliseconds(kPollIntervalMs)) {
        last_poll_ = now;
        {
            std::lock_guard<std::mutex> lock(controller_mutex_);
            controller_.pollOnce();
            runAutoInitializationLocked();
            publishSnapshot();
        }
    }

    {
        std::lock_guard<std::mutex> lock(controller_mutex_);
        runFaceTrackingLocked();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

void GimbalMonitor::shutdown(AppContext& /*ctx*/) {
    face_target_source_.shutdown();
    controller_.shutdown();
    LOGI("GimbalMonitor", "Shutdown");
}

GimbalCommandResult GimbalMonitor::executeBusinessCommand(const std::string& command,
                                                          int timeout_ms) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!enabled()) {
        GimbalCommandResult result;
        result.command = command;
        result.error = "gimbal_disabled";
        last_command_result_ = result;
        publishSnapshot();
        return result;
    }
    auto_init_running_ = true;
    auto result = controller_.executeBusinessCommand(command, timeout_ms);
    auto_init_running_ = false;
    last_command_result_ = result;
    publishSnapshot();
    return result;
}

}  // namespace edgefocus
