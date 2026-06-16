#include "DeviceManager.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"

namespace edgefocus {

DeviceManager::DeviceManager(SystemStateStore& store) : Module("DeviceManager"), store_(store) {}

bool DeviceManager::init(AppContext& /*ctx*/) {
    LOGI("DeviceManager", "Init DeviceManager");
    // Unblock bluetooth and bring up adapter for SimpleBLE
    system("rfkill unblock bluetooth 2>/dev/null");
    system("hciconfig hci0 up 2>/dev/null");
    return true;
}

bool DeviceManager::issueCommand(const InterventionCommand& cmd) {
    return command_queue_.push(cmd);
}

void DeviceManager::runOnce(AppContext& ctx) {
    auto now = std::chrono::steady_clock::now();

    // Sync BLE state from store
    bool ble_enabled_now = store_.snapshot().device.ble_enabled;
    if (ble_enabled_now && !ble_ctrl_.isEnabled()) {
        ble_ctrl_.startConnection();
    } else if (!ble_enabled_now && ble_ctrl_.isEnabled()) {
        ble_ctrl_.stopConnection();
    }

    if (auto opt_cmd = command_queue_.tryPop()) {
        const auto& cmd = *opt_cmd;
        LOGI("DeviceManager", "Executing command level: %d, message: %s",
             static_cast<int>(cmd.level), cmd.message.c_str());

        if (cmd.level == CommandLevel::LEVEL_1) {
            // Light notification: screen flash only
            screen_ctrl_.flashScreen();
        } else if (cmd.level == CommandLevel::LEVEL_2) {
            screen_ctrl_.flashScreen();
        } else if (cmd.level == CommandLevel::LEVEL_3) {
            screen_ctrl_.flashScreen();
            ble_ctrl_.sendAlert(static_cast<uint8_t>(cmd.level));
        } else if (cmd.level == CommandLevel::REST_SUGGESTION) {
            screen_ctrl_.flashScreen();
        }
    }

    // Update connection status only (never overwrite ble_enabled)
    static auto last_status_update = std::chrono::steady_clock::now();
    if (now - last_status_update > std::chrono::seconds(3)) {
        last_status_update = now;
        DeviceStatus status = store_.snapshot().device;
        status.camera_ok = true;
        status.bracelet_connected = status.ble_enabled && ble_ctrl_.isConnected();
        store_.updateDeviceStatus(status);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void DeviceManager::shutdown(AppContext& /*ctx*/) {
    LOGI("DeviceManager", "Shutdown");
}

} // namespace edgefocus
