#pragma once
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstdint>
#include "behavior/FocusState.h"
#include "policy/InterventionCommand.h"
#include "policy/ModeManager.h"
#include "storage/ReportGenerator.h"

namespace edgefocus {

// ---------------------------------------------------------------------------
// SystemStateStore — the single source of truth for UI and device layers.
//
// RULES:
//   - Behavior/vision/policy modules WRITE to this store.
//   - UI and DeviceManager only READ (via snapshot()).
//   - All fields are protected by a single mutex.
//   - snapshot() returns a consistent copy for lock-free reading.
// ---------------------------------------------------------------------------

struct SessionInfo {
    bool        active = false;
    bool        calibrating = false;           // true during calibration phase
    bool        calibration_completed = false;  // true after FSM exits CALIBRATING
    std::string session_id;                    // unique session identifier
    int64_t     start_ms = 0;
    int64_t     elapsed_ms = 0;
    int         distraction_count = 0;
    int         reminder_count = 0;
    int64_t     away_since_ms = 0;             // timestamp when AWAY state began (0 = not away)
    int64_t     away_timeout_ms = 30 * 60 * 1000; // auto-terminate after away for this long
    int64_t     target_duration_ms = 0;        // pomodoro target duration (0 = no target)
    int64_t     remaining_ms = 0;              // remaining time (target - elapsed)
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

struct DeviceStatus {
    SystemMode  system_mode = SystemMode::NORMAL;
    bool        camera_ok = false;
    bool        bracelet_connected = false;
    bool        ble_enabled = false;
    int         battery_percent = -1;  // -1 = unknown
};

struct FrameQualityInfo {
    float       score = 0.0f;       // 0.0 ~ 1.0
    std::string reason;             // e.g. "blur detected", "too dark"
    bool        guidance_needed = false;
};

// ---------------------------------------------------------------------------
// Complete state snapshot — returned by snapshot()
// ---------------------------------------------------------------------------

struct SystemSnapshot {
    // Focus
    FocusState  focus_state = FocusState::CALIBRATING;
    int64_t     focus_state_since_ms = 0;
    std::string focus_reason;

    // Session
    SessionInfo session;

    // Today
    TodayStats  today;

    // Reminder
    ActiveReminder reminder;

    // Device
    DeviceStatus device;

    // Frame quality
    FrameQualityInfo frame_quality;

    // Last report
    SessionReport last_report;

    // Timestamp of this snapshot
    int64_t snapshot_ms = 0;
};

// ---------------------------------------------------------------------------
// SystemStateStore
// ---------------------------------------------------------------------------

class SystemStateStore {
public:
    // Delete copy/move
    SystemStateStore(const SystemStateStore&) = delete;
    SystemStateStore& operator=(const SystemStateStore&) = delete;

    SystemStateStore() = default;

    // --- Writers (called by behavior/vision/policy modules) ---

    void updateFocusState(FocusState state, int64_t since_ms, const std::string& reason);
    void updateSession(const SessionInfo& info);
    void updateTodayStats(const TodayStats& stats);
    void updateReminder(CommandLevel level, const std::string& message, int64_t since_ms);
    void clearReminder();
    void updateDeviceStatus(const DeviceStatus& status);
    void updateFrameQuality(float score, const std::string& reason, bool guidance_needed);
    void updateLastReport(const SessionReport& report);

    // Increment helpers
    void incrementDistraction();
    void incrementReminder();

    // --- Session lifecycle ---
    void notifySessionChange();
    bool waitForSessionChange(std::unique_lock<std::mutex>& lock, int timeout_ms);
    void resetSession();
    std::mutex& mutexForCV() { return mutex_; }

    // --- Reader (called by UI) ---

    SystemSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable session_cv_;
    SystemSnapshot state_;
};

} // namespace edgefocus
