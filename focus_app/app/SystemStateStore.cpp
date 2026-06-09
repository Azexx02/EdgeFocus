#include "SystemStateStore.h"
#include <chrono>

namespace edgefocus {

// --- Writers ---

void SystemStateStore::updateFocusState(FocusState state, int64_t since_ms,
                                         const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.focus_state = state;
    state_.focus_state_since_ms = since_ms;
    state_.focus_reason = reason;
}

void SystemStateStore::updateSession(const SessionInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.session = info;
}

void SystemStateStore::updateTodayStats(const TodayStats& stats) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.today = stats;
}

void SystemStateStore::updateReminder(CommandLevel level, const std::string& message,
                                       int64_t since_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.reminder.level = level;
    state_.reminder.message = message;
    state_.reminder.since_ms = since_ms;
}

void SystemStateStore::clearReminder() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.reminder.level = CommandLevel::NONE;
    state_.reminder.message.clear();
    state_.reminder.since_ms = 0;
}

void SystemStateStore::updateDeviceStatus(const DeviceStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.device = status;
}

void SystemStateStore::updateFrameQuality(float score, const std::string& reason,
                                            bool guidance_needed) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.frame_quality.score = score;
    state_.frame_quality.reason = reason;
    state_.frame_quality.guidance_needed = guidance_needed;
}

void SystemStateStore::updateLastReport(const SessionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.last_report = report;
}

void SystemStateStore::incrementDistraction() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.session.distraction_count++;
}

void SystemStateStore::incrementReminder() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.session.reminder_count++;
}

// --- Reader ---

SystemSnapshot SystemStateStore::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    SystemSnapshot s = state_;
    s.snapshot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return s;
}

} // namespace edgefocus
