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

void normalizePowerStatus(DeviceStatus& status) {
    if (status.battery_percent < 0 || status.battery_percent > 100) {
        status.battery_percent = 67;
    }
    status.power_profile = status.battery_charging ? PowerProfile::HOME : PowerProfile::GO;
    if (status.battery_charging &&
        (status.power_source.empty() || status.power_source == "unknown" || status.power_source == "battery")) {
        status.power_source = "external";
    } else if (!status.battery_charging &&
               (status.power_source.empty() || status.power_source == "unknown" || status.power_source == "external")) {
        status.power_source = "battery";
    }
}
} // namespace

DeviceManager::DeviceManager(SystemStateStore& store) : Module("DeviceManager"), store_(store) {}

bool DeviceManager::init(AppContext& /*ctx*/) {
    LOGI("DeviceManager", "Init DeviceManager");
    auto snap = store_.snapshot();
    DeviceStatus status = snap.device;
    status.board_screen_available = snap.preferences.screen_output_route != "phone" &&
                                    ScreenController::isAvailable();
    status.board_sound_available = snap.preferences.sound_output_route != "phone" &&
                                   SoundController::isAvailable();
    normalizePowerStatus(status);
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
    bool did_work = false;

    // Sync BLE state from store
    bool ble_enabled_now = store_.snapshot().device.ble_enabled;
    if (ble_enabled_now && !ble_ctrl_.isEnabled()) {
        ble_ctrl_.startConnection();
    } else if (!ble_enabled_now && ble_ctrl_.isEnabled()) {
        ble_ctrl_.stopConnection();
    }

    if (auto action = action_queue_.tryPop()) {
        executeAction(*action);
        did_work = true;
    } else if (auto cmd = command_queue_.tryPop()) {
        executeCommand(*cmd);
        did_work = true;
    }

    // Update connection status only (never overwrite ble_enabled)
    static auto last_status_update = std::chrono::steady_clock::now();
    if (now - last_status_update > std::chrono::seconds(3)) {
        last_status_update = now;
        auto snap = store_.snapshot();
        DeviceStatus status = snap.device;
        status.bracelet_connected = status.ble_enabled && ble_ctrl_.isConnected();
        status.band_last_alert_level = ble_ctrl_.lastAlertLevel();
        status.band_last_alert_pattern = ble_ctrl_.lastAlertPattern();
        status.band_write_attempt_count = ble_ctrl_.writeAttemptCount();
        status.band_last_write_status = ble_ctrl_.lastWriteStatus();
        status.board_screen_available = snap.preferences.screen_output_route != "phone" &&
                                        ScreenController::isAvailable();
        status.board_sound_available = snap.preferences.sound_output_route != "phone" &&
                                       SoundController::isAvailable();
        normalizePowerStatus(status);
        store_.updateDeviceStatus(status);
    }

    auto status = store_.snapshot().device;
    auto normalized = status;
    normalizePowerStatus(normalized);
    if (normalized.battery_percent != status.battery_percent ||
        normalized.battery_charging != status.battery_charging ||
        normalized.power_source != status.power_source ||
        normalized.power_profile != status.power_profile) {
        store_.updateDeviceStatus(normalized);
        status = normalized;
    }
    const int idle_ms = (!did_work && status.power_profile == PowerProfile::GO) ? 150 : 50;
    std::this_thread::sleep_for(std::chrono::milliseconds(idle_ms));
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
    if (cmd.level == CommandLevel::LEVEL_1) {
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
        if (ble_ctrl_.sendAlert(alert_level)) {
            status.last_delivered_channels_mask |= channel_mask;
        }
    } else if (action.channel == NotificationChannel::SOUND) {
        if (store_.snapshot().preferences.sound_output_route != "phone") {
            sound_ctrl_.playReminderSound(store_.snapshot().preferences.sound_volume);
            status.board_sound_available = SoundController::isAvailable();
        } else {
            status.board_sound_available = false;
        }
        if (status.board_sound_available) status.last_delivered_channels_mask |= channel_mask;
    } else {
        if (store_.snapshot().preferences.screen_output_route != "phone") {
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
    normalizePowerStatus(status);
    store_.updateDeviceStatus(status);
}

} // namespace edgefocus
