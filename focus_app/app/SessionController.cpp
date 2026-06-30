#include "SessionController.h"
#include "Logger.h"
#include "storage/StorageEvent.h"
#include "SessionPhase.h"

#include <chrono>
#include <algorithm>

namespace edgefocus {

namespace {
constexpr int kDefaultTargetDurationMin = 25;

int64_t monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t wallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

SessionController::SessionController(SystemStateStore& store, SQLiteStore& sqlite_store)
    : store_(store), sqlite_store_(sqlite_store) {}

SessionController::StartResult SessionController::startSession(int target_duration_min,
                                                               const std::string& source) {
    StartResult result;
    auto snap = store_.snapshot();
    if (snap.session.active) {
        result.success = true;
        result.already_active = true;
        result.session = snap.session;
        return result;
    }

    const int64_t start_wall_ms = wallClockMs();
    store_.clearPhoneSignal();
    SessionInfo s;
    s.active = true;
    s.paused = false;
    s.calibrating = true;
    s.calibration_completed = false;
    s.session_id = "session_" + std::to_string(start_wall_ms);
    s.start_ms = monotonicMs();
    const int effective_target_min = target_duration_min > 0 ? target_duration_min : kDefaultTargetDurationMin;
    s.target_duration_ms = static_cast<int64_t>(effective_target_min) * 60000;
    s.remaining_ms = s.target_duration_ms;
    s.away_timeout_ms = snap.session.away_timeout_ms;

    store_.updateSession(s);
    store_.updateFocusState(FocusState::CALIBRATING, monotonicMs(), "Session precheck started");
    store_.notifySessionChange();

    sqlite_store_.saveSession(s.session_id, start_wall_ms, s.target_duration_ms);
    sqlite_store_.logEvent({StorageEventType::SessionStart, s.session_id, s.start_ms,
                            source + " session started"});

    LOGI("SessionController", "Session started: %s source=%s target=%lldms",
         s.session_id.c_str(), source.c_str(), (long long)s.target_duration_ms);

    result.success = true;
    result.session = s;
    return result;
}

bool SessionController::pauseSession(const std::string& reason) {
    auto snap = store_.snapshot();
    if (!snap.session.active) {
        LOGW("SessionController", "Pause failed: no active session");
        return false;
    }
    if (snap.session.paused) {
        LOGI("SessionController", "Pause ignored: session already paused");
        return true;
    }

    const int64_t now_ms = monotonicMs();
    SessionInfo s = snap.session;
    if (s.start_ms > 0) {
        s.elapsed_ms = now_ms - s.start_ms;
        if (s.elapsed_ms < 0) s.elapsed_ms = 0;
        if (s.target_duration_ms > 0) {
            s.remaining_ms = s.target_duration_ms - s.elapsed_ms;
            if (s.remaining_ms < 0) s.remaining_ms = 0;
        }
    }
    s.paused = true;
    store_.updateSession(s);
    store_.updateFocusState(FocusState::PAUSED, now_ms, reason);
    store_.notifySessionChange();
    LOGI("SessionController", "Session paused: %s reason=%s", s.session_id.c_str(), reason.c_str());
    return true;
}

bool SessionController::resumeSession(const std::string& reason) {
    auto snap = store_.snapshot();
    if (!snap.session.active) {
        LOGW("SessionController", "Resume failed: no active session");
        return false;
    }
    if (!snap.session.paused) {
        LOGI("SessionController", "Resume ignored: session is not paused");
        return true;
    }

    int64_t now_ms = monotonicMs();
    SessionInfo s = snap.session;
    s.paused = false;
    s.start_ms = now_ms - s.elapsed_ms;
    store_.updateSession(s);
    store_.updateFocusState(FocusState::FOCUSED, now_ms, reason);
    store_.notifySessionChange();
    LOGI("SessionController", "Session resumed: %s reason=%s", s.session_id.c_str(), reason.c_str());
    return true;
}

SessionController::EndResult SessionController::endSession(const std::string& reason) {
    EndResult result;
    auto snap = store_.snapshot();
    if (!snap.session.active) {
        result.success = false;
        result.message = "No active session";
        return result;
    }

    SessionInfo s = snap.session;
    if (!s.paused && s.start_ms > 0 && s.elapsed_ms <= 0) {
        const int64_t refreshed_elapsed = monotonicMs() - s.start_ms;
        s.elapsed_ms = std::max<int64_t>(0, refreshed_elapsed);
    }
    int64_t focus_ms = s.focus_accumulated_ms;
    if (focus_ms == 0 && deriveSessionPhase(s) == SessionPhase::FOCUSING &&
        s.distraction_count == 0 && s.phone_distraction_count == 0) {
        focus_ms = std::max<int64_t>(0, s.elapsed_ms);
    }
    s.active = false;
    s.paused = false;
    store_.updateSession(s);
    store_.notifySessionChange();

    SessionReport report;
    report.session_id = s.session_id;
    report.total_duration = s.elapsed_ms / 1000;
    report.focus_duration = focus_ms / 1000;
    report.distraction_count = s.distraction_count;
    report.reminder_count = s.reminder_count;
    report.phone_distraction_count = s.phone_distraction_count;
    store_.updateLastReport(report);

    TodayStats today = snap.today;
    today.total_seconds += report.total_duration;
    today.focus_seconds += report.focus_duration;
    today.sessions++;
    today.events += report.distraction_count;
    today.phone_distraction_count += report.phone_distraction_count;
    store_.updateTodayStats(today);

    if (!s.session_id.empty()) {
        sqlite_store_.updateSessionEnd(s.session_id, wallClockMs(), report.total_duration,
            report.focus_duration, s.distraction_count, s.reminder_count,
            s.phone_distraction_count, reason);
        sqlite_store_.logEvent({StorageEventType::SessionEnd, s.session_id, monotonicMs(),
                                reason});
    }

    store_.resetSession();

    result.success = true;
    result.report = report;
    result.target_duration_ms = s.target_duration_ms;
    return result;
}

int SessionController::closeInterruptedSessions() {
    return sqlite_store_.closeOpenSessions(wallClockMs(), "interrupted");
}

} // namespace edgefocus
