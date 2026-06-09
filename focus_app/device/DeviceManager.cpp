#include "DeviceManager.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"

namespace edgefocus {

DeviceManager::DeviceManager(SystemStateStore& store) : Module("DeviceManager"), store_(store) {}

bool DeviceManager::init(AppContext& /*ctx*/) {
    LOGI("DeviceManager", "Init DeviceManager");
    return true;
}

bool DeviceManager::issueCommand(const InterventionCommand& cmd) {
    return command_queue_.push(cmd);
}

void DeviceManager::runOnce(AppContext& ctx) {
    // No mock alerts.
    auto now = std::chrono::steady_clock::now();

    if (auto opt_cmd = command_queue_.tryPop()) {
        const auto& cmd = *opt_cmd;
        LOGI("DeviceManager", "Executing command level: %d, message: %s",
             static_cast<int>(cmd.level), cmd.message.c_str());

        if (cmd.level == CommandLevel::LEVEL_2) {
            screen_ctrl_.flashScreen();
        } else if (cmd.level == CommandLevel::LEVEL_3) {
            screen_ctrl_.flashScreen();
            ble_ctrl_.sendAlert();
        }
    }

    // Update SystemStateStore with device status periodically
    static auto last_status_update = std::chrono::steady_clock::now();
    if (now - last_status_update > std::chrono::seconds(5)) {
        last_status_update = now;

        DeviceStatus status;
        status.system_mode = SystemMode::NORMAL;  // Will be updated by ModeManager
        status.camera_ok = true;  // Will be updated by CameraPipeline
        status.bracelet_connected = false;  // Will be updated by BLE controller
        status.battery_percent = -1;  // Will be updated by battery monitor
        store_.updateDeviceStatus(status);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void DeviceManager::shutdown(AppContext& /*ctx*/) {
    LOGI("DeviceManager", "Shutdown");
}

} // namespace edgefocus
