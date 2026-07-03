#pragma once
#include <mutex>
#include <condition_variable>
#include <string>
#include <functional>
#include <cstdint>
#include "behavior/FocusState.h"
#include "behavior/EventDetector.h"
#include "policy/InterventionCommand.h"
#include "policy/ModeManager.h"
#include "storage/ReportGenerator.h"

namespace edgefocus {

struct SessionInfo {
    bool        active = false;
    bool        paused = false;
    bool        calibrating = false;
    bool        calibration_completed = false;
    std::string precheck_owner = "board";
    std::string session_id;
    int64_t     start_ms = 0;
    int64_t     elapsed_ms = 0;
    int64_t     focus_accumulated_ms = 0;
    int         distraction_count = 0;
    int         reminder_count = 0;
    int         phone_distraction_count = 0;
    int         current_phone_distraction_level = 1;
    int64_t     away_since_ms = 0;
    int64_t     away_timeout_ms = 30 * 60 * 1000;
    int64_t     target_duration_ms = 0;
    int64_t     remaining_ms = 0;
};

struct TodayStats {
    int64_t total_seconds = 0;
    int64_t focus_seconds = 0;
    int     sessions = 0;
    int     events = 0;
    int     phone_distraction_count = 0;
};

struct ActiveReminder {
    CommandLevel level = CommandLevel::NONE;
    std::string  message;
    int64_t      since_ms = 0;
};

struct BehaviorContext {
    int         risk_score = 0;
    bool        vision_signal = false;
    bool        phone_signal = false;
    int         phone_distraction_level = -1;
    int         phone_usage_state = -1;
    std::string reason;
};

struct DeviceStatus {
    SystemMode  system_mode = SystemMode::NORMAL;
    NotificationMode notification_mode = NotificationMode::NORMAL;
    PowerProfile power_profile = PowerProfile::GO;
    bool        camera_ok = false;
    bool        bracelet_connected = false;
    bool        ble_enabled = false;
    int         battery_percent = -1;
    int         battery_voltage_mv = -1;
    bool        battery_charging = false;
    std::string power_source = "unknown";
    int         band_last_alert_level = 0;
    int         band_last_alert_pattern = 0;
    int         band_write_attempt_count = 0;
    std::string band_last_write_status;
    bool        gimbal_connected = false;
    bool        gimbal_online = false;
    int         gimbal_boost = 0;
    int         gimbal_motor = 0;
    int         gimbal_en = 0;
    int         gimbal_ix = 0;
    int         gimbal_iy = 0;
    bool        gimbal_safe = false;
    bool        gimbal_xhome = false;
    bool        gimbal_yhome = false;
    bool        gimbal_calibrated = false;
    float       gimbal_angle_x = 0.0f;
    float       gimbal_angle_y = 0.0f;
    int64_t     gimbal_last_seen_ms = 0;
    std::string gimbal_last_error;
    bool        gimbal_initializing = false;
    bool        gimbal_auto_initialized = false;
    std::string gimbal_last_command;
    std::string gimbal_last_command_status;
    std::string gimbal_last_command_response;
    bool        board_screen_available = false;
    bool        board_sound_available = false;
    int         last_attempted_channels_mask = 0;
    int         last_delivered_channels_mask = 0;
};

struct FrameQualityInfo {
    float       score = 0.0f;
    std::string reason;
    bool        guidance_needed = false;
    std::string distance_hint;
    std::string pose_hint;
    bool        precheck_ready = false;
};

struct CameraPreviewImage {
    bool        available = false;
    int64_t     timestamp_ms = 0;
    int64_t     sequence = 0;
    int         width = 0;
    int         height = 0;
    std::string pixel_format;
    std::string encoding;
    std::string source;
    std::string data_base64;
};

#ifdef EDGEFOCUS_WITH_DEMO_HTTP
struct DemoOverride {
    bool        focus_override_enabled = false;
    FocusState  focus_state = FocusState::FOCUSED;
    int         risk_score = 0;
    std::string reason;
    bool        precheck_guidance_override_enabled = false;
    std::string distance_hint;
    std::string pose_hint;
    bool        precheck_ready = false;
};
#endif

struct Preferences {
    int         system_mode = 0;
    NotificationMode notification_mode = NotificationMode::NORMAL;
    PowerProfile power_profile = PowerProfile::GO;
    int         away_timeout_min = 15;
    int         default_focus_duration_min = 25;
    float       vibration_strength = 0.5f;
    float       sound_volume = 0.7f;
    int         led_brightness = 75;
    float       reminder_sensitivity = 0.5f;
    // Reminder channel bitmasks:
    // SCREEN=1, BAND=2, SOUND=4
    // LIGHT=8 is legacy input compatibility and never returned as a normal
    // channel option.
    int         normal_channels_mask = 7;
    int         silent_channels_mask = 3;
    std::string screen_output_route = "board";
    std::string sound_output_route = "board";
    bool        gimbal_enabled = true;
};

struct SystemSnapshot {
    FocusState  focus_state = FocusState::CALIBRATING;
    int64_t     focus_state_since_ms = 0;
    std::string focus_reason;

    SessionInfo session;
    TodayStats  today;
    ActiveReminder reminder;
    DeviceStatus device;
    FrameQualityInfo frame_quality;
    CameraPreviewImage camera_preview_image;
    SessionReport last_report;
    Preferences preferences;
    BehaviorContext behavior_context;
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    DemoOverride demo_override;
#endif

    int64_t snapshot_ms = 0;
};

class SystemStateStore {
public:
    SystemStateStore(const SystemStateStore&) = delete;
    SystemStateStore& operator=(const SystemStateStore&) = delete;
    SystemStateStore() = default;

    void updateFocusState(FocusState state, int64_t since_ms, const std::string& reason);
    void updateSession(const SessionInfo& info);
    bool updateSessionIfCurrent(const SessionInfo& info);
    void updateTodayStats(const TodayStats& stats);
    void updateReminder(CommandLevel level, const std::string& message, int64_t since_ms);
    void clearReminder();
    void updateDeviceStatus(const DeviceStatus& status);
    void updateFrameQuality(float score, const std::string& reason, bool guidance_needed);
    void updatePrecheckGuidance(const std::string& distance_hint, const std::string& pose_hint,
                                bool precheck_ready);
    void updateCameraPreviewImage(const CameraPreviewImage& preview);
    void updateLastReport(const SessionReport& report);
    void updatePreferences(const Preferences& prefs);
    void updateBehaviorContext(const BehaviorContext& context);
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    void updateDemoOverride(const DemoOverride& demo_override);
    void clearDemoOverride();
#endif

    void incrementDistraction();
    void incrementReminder();

    void notifySessionChange();
    bool waitForSessionChange(std::unique_lock<std::mutex>& lock, int timeout_ms);
    std::unique_lock<std::mutex> lockSessionLifecycle();
    void resetSession();
    std::mutex& mutexForCV() { return mutex_; }

    // Phone signal buffer (BleGattServer writes, BehaviorEngine reads)
    void setPhoneSignal(const PhoneUsageSignal& sig);
    bool consumePhoneSignal(PhoneUsageSignal& sig_out);
    void clearPhoneSignal();

    // Push event callback (BehaviorEngine calls, BleGattServer registers)
    using PushCallback = std::function<void(const std::string& event_json)>;
    void setPushCallback(PushCallback cb);
    void pushEvent(const std::string& event_json);

    SystemSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    mutable std::mutex session_lifecycle_mutex_;
    std::condition_variable session_cv_;
    SystemSnapshot state_;

    PhoneUsageSignal pending_phone_sig_;
    bool has_phone_signal_ = false;
    PushCallback push_callback_;
};

} // namespace edgefocus
