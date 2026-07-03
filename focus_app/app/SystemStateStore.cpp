#include "SystemStateStore.h"
#include <chrono>

namespace edgefocus {

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

void SystemStateStore::updatePrecheckGuidance(const std::string& distance_hint,
                                               const std::string& pose_hint,
                                               bool precheck_ready) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.frame_quality.distance_hint = distance_hint;
    state_.frame_quality.pose_hint = pose_hint;
    state_.frame_quality.precheck_ready = precheck_ready;
}

void SystemStateStore::updateCameraPreviewImage(const CameraPreviewImage& preview) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.camera_preview_image = preview;
}

void SystemStateStore::updateLastReport(const SessionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.last_report = report;
}

void SystemStateStore::updatePreferences(const Preferences& prefs) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.preferences = prefs;
}

void SystemStateStore::updateBehaviorContext(const BehaviorContext& context) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.behavior_context = context;
}

void SystemStateStore::updateDemoOverride(const DemoOverride& demo_override) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.demo_override = demo_override;
}

void SystemStateStore::clearDemoOverride() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.demo_override = DemoOverride{};
}

void SystemStateStore::incrementDistraction() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.session.distraction_count++;
}

void SystemStateStore::incrementReminder() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.session.reminder_count++;
}

void SystemStateStore::notifySessionChange() {
    session_cv_.notify_all();
}

bool SystemStateStore::waitForSessionChange(std::unique_lock<std::mutex>& lock, int timeout_ms) {
    session_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                         [this]() { return state_.session.active; });
    return state_.session.active;
}

void SystemStateStore::resetSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.session = SessionInfo{};
    state_.focus_state = FocusState::CALIBRATING;
    state_.focus_reason.clear();
    state_.reminder.level = CommandLevel::NONE;
    state_.reminder.message.clear();
    state_.reminder.since_ms = 0;
    state_.behavior_context = BehaviorContext{};
    // Clear phone signal buffer so previous session's signal doesn't leak
    has_phone_signal_ = false;
    pending_phone_sig_ = {};
}

// Phone signal buffer

void SystemStateStore::setPhoneSignal(const PhoneUsageSignal& sig) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_phone_sig_ = sig;
    has_phone_signal_ = true;
}

bool SystemStateStore::consumePhoneSignal(PhoneUsageSignal& sig_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_phone_signal_) return false;
    sig_out = pending_phone_sig_;
    has_phone_signal_ = false;
    return true;
}

void SystemStateStore::clearPhoneSignal() {
    std::lock_guard<std::mutex> lock(mutex_);
    has_phone_signal_ = false;
    pending_phone_sig_ = {};
}

// Push event callback

void SystemStateStore::setPushCallback(PushCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    push_callback_ = std::move(cb);
}

void SystemStateStore::pushEvent(const std::string& event_json) {
    std::function<void(const std::string&)> cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = push_callback_;
    }
    if (cb) cb(event_json);
}

// Reader

SystemSnapshot SystemStateStore::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    SystemSnapshot s = state_;
    s.snapshot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return s;
}

} // namespace edgefocus
