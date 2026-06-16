#include "BehaviorEngine.h"
#include "app/AppContext.h"
#include "storage/StorageEvent.h"

namespace edgefocus {

BehaviorEngine::BehaviorEngine(BoundedQueue<VisionResult>& vision_queue, SystemStateStore& store, SQLiteStore& sqlite_store)
    : Module("BehaviorEngine"), vision_queue_(vision_queue), store_(store), sqlite_store_(sqlite_store) {}

bool BehaviorEngine::init(AppContext& /*ctx*/) {
    LOGI("BehaviorEngine", "Initialized");
    return true;
}

void BehaviorEngine::runOnce(AppContext& /*ctx*/) {
    SystemSnapshot snap = store_.snapshot();

    // --- IDLE: session not active ---
    if (!snap.session.active) {
        if (session_was_active_) {
            // Session just ended: update today stats
            SessionInfo ended_session = snap.session;
            TodayStats today = snap.today;
            today.total_seconds += ended_session.elapsed_ms / 1000;
            today.focus_seconds += focus_accumulated_ms_ / 1000;
            today.sessions++;
            today.events += ended_session.distraction_count;
            store_.updateTodayStats(today);

            // Reset FSM and drain queue
            fsm_.reset();
            while (vision_queue_.tryPop().has_value()) {}
            session_was_active_ = false;
            focus_accumulated_ms_ = 0;
            last_focus_update_ms_ = 0;
            LOGI("BehaviorEngine", "Session ended, FSM reset");
        }
        // Block until session becomes active
        std::unique_lock<std::mutex> lock(store_.mutexForCV());
        store_.waitForSessionChange(lock, 1000);
        return;
    }

    session_was_active_ = true;

    // --- Update elapsed time using system clock (independent of vision data) ---
    {
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        SessionInfo s = store_.snapshot().session;
        if (s.active && s.start_ms > 0) {
            s.elapsed_ms = now_ms - s.start_ms;
            if (s.target_duration_ms > 0) {
                s.remaining_ms = s.target_duration_ms - s.elapsed_ms;
                if (s.remaining_ms <= 0) {
                    s.remaining_ms = 0;
                    LOGI("BehaviorEngine", "Target duration reached, auto-terminating session");
                    s.active = false;
                    store_.updateSession(s);
                    store_.notifySessionChange();

                    TodayStats today = store_.snapshot().today;
                    today.total_seconds += s.elapsed_ms / 1000;
                    today.sessions++;
                    store_.updateTodayStats(today);
                    return;
                }
            }
            store_.updateSession(s);
        }
    }

    // --- ACTIVE: process vision results ---
    auto result_opt = vision_queue_.tryPop();
    if (!result_opt.has_value()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return;
    }

    const auto& vision_result = result_opt.value();
    FocusState old_state = fsm_.getCurrentState();
    fsm_.processFrame(vision_result);
    FocusState new_state = fsm_.getCurrentState();

    // --- Handle calibration completion ---
    if (old_state == FocusState::CALIBRATING && new_state == FocusState::FOCUSED) {
        SessionInfo s = store_.snapshot().session;
        s.calibrating = false;
        s.calibration_completed = true;
        store_.updateSession(s);
        LOGI("BehaviorEngine", "Calibration complete, monitoring started");
    }

    // --- State change handling ---
    if (old_state != new_state) {
        std::string reason = "State transition";
        store_.updateFocusState(new_state, vision_result.timestamp_ms, reason);

        // Track away duration for auto-timeout
        SessionInfo s = store_.snapshot().session;
        if (new_state == FocusState::AWAY) {
            if (s.away_since_ms == 0) {
                s.away_since_ms = vision_result.timestamp_ms;
                store_.updateSession(s);
            }
        } else {
            // Reset away timer when no longer away
            if (s.away_since_ms != 0) {
                s.away_since_ms = 0;
                store_.updateSession(s);
            }
        }

        // Log state change
        StorageEvent ev;
        ev.session_id = store_.snapshot().session.session_id;
        ev.type = StorageEventType::StateChange;
        ev.timestamp = vision_result.timestamp_ms;
        ev.data = "Changed to " + std::to_string(static_cast<int>(new_state));
        sqlite_store_.logEvent(ev);
    }

    // --- Away timeout check ---
    {
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        snap = store_.snapshot();
        if (snap.session.away_since_ms > 0 && snap.session.active) {
            int64_t away_duration = now_ms - snap.session.away_since_ms;
            int64_t timeout_ms = snap.session.away_timeout_ms;
            if (timeout_ms <= 0) timeout_ms = 30 * 60 * 1000;

            if (away_duration >= timeout_ms) {
                LOGI("BehaviorEngine", "Away for %lld ms, auto-terminating session",
                     (long long)away_duration);
                SessionInfo s = snap.session;
                s.active = false;
                store_.updateSession(s);
                store_.notifySessionChange();
            }
        }
    }

    // --- Track focus time accumulation ---
    {
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (new_state == FocusState::FOCUSED) {
            if (last_focus_update_ms_ > 0) {
                focus_accumulated_ms_ += now_ms - last_focus_update_ms_;
            }
            last_focus_update_ms_ = now_ms;
        } else {
            last_focus_update_ms_ = now_ms;
        }
    }
}

void BehaviorEngine::shutdown(AppContext& /*ctx*/) {
    LOGI("BehaviorEngine", "Shutdown");
}

} // namespace edgefocus
