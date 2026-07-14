#include "session/LegacySessionProjectionFeature.h"

#include "app/SessionController.h"
#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "storage/StorageEvent.h"

#include <algorithm>
#include <chrono>

namespace edgefocus::session {

LegacySessionProjectionFeature::LegacySessionProjectionFeature(
    SystemStateStore& store,
    SQLiteStore& sqlite_store)
    : store_(store), sqlite_store_(sqlite_store) {}

std::int64_t LegacySessionProjectionFeature::monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

const char* LegacySessionProjectionFeature::stopReasonName(StopReason reason) {
    switch (reason) {
        case StopReason::Completed: return "completed";
        case StopReason::Cancelled: return "cancelled";
        case StopReason::Interrupted: return "interrupted";
        case StopReason::PrepareFailed: return "prepare_failed";
        case StopReason::ProcessShutdown: return "process_shutdown";
    }
    return "unknown";
}

PrepareResult LegacySessionProjectionFeature::prepare(const SessionContext& context) {
    const auto snapshot = store_.snapshot();
    if (snapshot.session.active) {
        return PrepareResult::failure("legacy session projection already active");
    }

    context_ = context;
    SessionInfo state;
    state.active = true;
    state.paused = false;
    state.calibrating = true;
    state.calibration_completed = false;
    state.precheck_owner = "app";
    state.session_id = context.session_id;
    state.start_ms = context.started_monotonic_ms;
    state.target_duration_ms = context.target_duration_ms;
    state.remaining_ms = context.target_duration_ms;
    state.away_timeout_ms = static_cast<std::int64_t>(snapshot.preferences.away_timeout_min) * 60000;

    store_.clearPhoneSignal();
    store_.updateSession(state);
    store_.updateFocusState(FocusState::CALIBRATING, context.started_monotonic_ms,
                            "Session precheck started");
    store_.notifySessionChange();

    if (!sqlite_store_.saveSession(
            context.session_id, context.started_wall_ms, context.target_duration_ms)) {
        store_.resetSession();
        store_.notifySessionChange();
        return PrepareResult::failure("failed to persist session start");
    }
    sqlite_store_.logEvent({StorageEventType::SessionStart,
                            context.session_id,
                            context.started_monotonic_ms,
                            "orchestrated session started"});
    active_ = true;
    health_ = {true, false, "ready", {}, monotonicMs()};
    return PrepareResult::success();
}

void LegacySessionProjectionFeature::enterPhase(Phase phase) {
    if (!active_) return;
    auto snapshot = store_.snapshot();
    SessionInfo state = snapshot.session;
    const auto now_ms = monotonicMs();

    if (phase == Phase::Precheck) {
        state.paused = false;
        state.calibrating = true;
        state.calibration_completed = false;
        store_.updateSession(state);
        store_.updateFocusState(FocusState::CALIBRATING, now_ms, "Session precheck");
    } else if (phase == Phase::Focusing) {
        state.paused = false;
        if (!state.calibration_completed) {
            state.calibrating = false;
            state.calibration_completed = true;
            state.start_ms = now_ms;
            state.elapsed_ms = 0;
            state.remaining_ms = state.target_duration_ms;
            state.focus_accumulated_ms = 0;
        } else {
            state.start_ms = now_ms - state.elapsed_ms;
        }
        store_.updateSession(state);
        store_.updateFocusState(FocusState::FOCUSED, now_ms, "Session focusing");
    } else if (phase == Phase::Paused) {
        if (!state.paused && state.calibration_completed) {
            state.elapsed_ms = std::max<std::int64_t>(0, now_ms - state.start_ms);
            state.remaining_ms = std::max<std::int64_t>(0, state.target_duration_ms - state.elapsed_ms);
        }
        state.paused = true;
        store_.updateSession(state);
        store_.updateFocusState(FocusState::PAUSED, now_ms, "Session paused");
    }
    store_.notifySessionChange();
    health_.updated_at_ms = now_ms;
}

void LegacySessionProjectionFeature::stop(StopReason reason) noexcept {
    if (!active_) return;
    try {
        SessionController controller(store_, sqlite_store_);
        controller.endSession(stopReasonName(reason));
    } catch (...) {
        store_.resetSession();
        store_.notifySessionChange();
    }
    active_ = false;
    health_ = {false, false, "stopped", {}, monotonicMs()};
}

FeatureHealth LegacySessionProjectionFeature::health() const {
    return health_;
}

}  // namespace edgefocus::session
