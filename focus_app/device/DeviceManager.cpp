#include "DeviceManager.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"

namespace edgefocus {

namespace {
int maskForChannel(NotificationChannel channel) {
    switch (channel) {
        case NotificationChannel::SCREEN:
        case NotificationChannel::LIGHT:
            return kNotificationChannelScreenMask;
        case NotificationChannel::BAND:
            return kNotificationChannelBandMask;
        case NotificationChannel::SOUND:
            return kNotificationChannelSoundMask;
        default:
            return 0;
    }
}
} // namespace

DeviceManager::DeviceManager(SystemStateStore& store) : Module("DeviceManager"), store_(store) {}

bool DeviceManager::init(AppContext& /*ctx*/) {
    LOGI("DeviceManager", "Init DeviceManager");
    auto snap = store_.snapshot();
    DeviceStatus status = snap.device;
    const bool board_output_enabled = snap.preferences.board_output_route != "phone";
    status.board_screen_available = board_output_enabled && ScreenController::isAvailable();
    status.board_sound_available = board_output_enabled && SoundController::isAvailable();
    store_.updateDeviceStatus(status);
    return true;
}

bool DeviceManager::issueCommand(const InterventionCommand& cmd) {
    return command_queue_.push(cmd);
}

bool DeviceManager::issueAction(const DeviceAction& action) {
    return action_queue_.push(action);
}

void DeviceManager::runOnce(AppContext& /*ctx*/) {
    auto now = std::chrono::steady_clock::now();

    // Sync BLE state from store
    bool ble_enabled_now = store_.snapshot().device.ble_enabled;
    if (ble_enabled_now && !ble_ctrl_.isEnabled()) {
        ble_ctrl_.startConnection();
    } else if (!ble_enabled_now && ble_ctrl_.isEnabled()) {
        ble_ctrl_.stopConnection();
    }

    if (auto action = action_queue_.tryPop()) {
        executeAction(*action);
    } else if (auto cmd = command_queue_.tryPop()) {
        executeCommand(*cmd);
    }

    // Update connection status only (never overwrite ble_enabled)
    static auto last_status_update = std::chrono::steady_clock::now();
    if (now - last_status_update > std::chrono::seconds(3)) {
        last_status_update = now;
        auto snap = store_.snapshot();
        DeviceStatus status = snap.device;
        const bool board_output_enabled = snap.preferences.board_output_route != "phone";
        status.bracelet_connected = status.ble_enabled && ble_ctrl_.isConnected();
        status.band_last_alert_level = ble_ctrl_.lastAlertLevel();
        status.band_last_alert_pattern = ble_ctrl_.lastAlertPattern();
        status.band_write_attempt_count = ble_ctrl_.writeAttemptCount();
        status.band_last_write_status = ble_ctrl_.lastWriteStatus();
        status.board_screen_available = board_output_enabled && ScreenController::isAvailable();
        status.board_sound_available = board_output_enabled && SoundController::isAvailable();
        store_.updateDeviceStatus(status);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void DeviceManager::shutdown(AppContext& /*ctx*/) {
    ble_ctrl_.stopConnection();
    LOGI("DeviceManager", "Shutdown");
}

void DeviceManager::executeCommand(const InterventionCommand& cmd) {
    LOGI("DeviceManager", "Executing command level: %d, message: %s",
         static_cast<int>(cmd.level), cmd.message.c_str());

    // Legacy behavior: command-level routing maps ALERT levels directly to default
    // legacy channels. New channel routing is expected to pass through issueAction().
    if (cmd.level == CommandLevel::LEVEL_1 || cmd.level == CommandLevel::REST_SUGGESTION) {
        executeAction({NotificationChannel::SCREEN, cmd.level});
    } else if (cmd.level == CommandLevel::LEVEL_2 || cmd.level == CommandLevel::LEVEL_3) {
        executeAction({NotificationChannel::SCREEN, cmd.level});
        executeAction({NotificationChannel::BAND, cmd.level});
    }
}

void DeviceManager::executeAction(const DeviceAction& action) {
    ++executed_command_count_;
    last_executed_level_ = action.level;

    DeviceStatus status = store_.snapshot().device;
    const int channel_mask = maskForChannel(action.channel);
    status.last_attempted_channels_mask |= channel_mask;

    if (action.channel == NotificationChannel::BAND) {
        uint8_t alert_level = static_cast<uint8_t>(action.level);
        if (alert_level < 1) alert_level = 1;
        if (alert_level > 3) alert_level = 3;
        ble_ctrl_.sendAlert(alert_level);
        status.last_delivered_channels_mask |= channel_mask;
    } else if (action.channel == NotificationChannel::SOUND) {
        if (store_.snapshot().preferences.board_output_route != "phone") {
            sound_ctrl_.playReminderSound(store_.snapshot().preferences.sound_volume);
            status.board_sound_available = SoundController::isAvailable();
        } else {
            status.board_sound_available = false;
        }
        if (status.board_sound_available) status.last_delivered_channels_mask |= channel_mask;
    } else {
        if (store_.snapshot().preferences.board_output_route != "phone") {
            screen_ctrl_.flashScreen();
            status.board_screen_available = ScreenController::isAvailable();
        } else {
            status.board_screen_available = false;
        }
        if (status.board_screen_available) status.last_delivered_channels_mask |= channel_mask;
    }

    // Update band diagnostics for visibility in push/state snapshots.
    status.band_last_alert_level = ble_ctrl_.lastAlertLevel();
    status.band_last_alert_pattern = ble_ctrl_.lastAlertPattern();
    status.band_write_attempt_count = ble_ctrl_.writeAttemptCount();
    status.band_last_write_status = ble_ctrl_.lastWriteStatus();
    store_.updateDeviceStatus(status);
}

} // namespace edgefocus
