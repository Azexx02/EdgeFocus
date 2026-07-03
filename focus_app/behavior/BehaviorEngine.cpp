#include "BehaviorEngine.h"
#include "app/AppContext.h"
#include "app/SessionController.h"
#include "app/SessionPhase.h"
#include "storage/StorageEvent.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace edgefocus {

namespace {
constexpr int64_t kPrecheckGuidanceDebounceMs = 1200;

int64_t monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int clampPhoneDistractionLevel(int level) {
    if (level < 1) return 1;
    if (level > 3) return 3;
    return level;
}

int estimatePhoneRisk(int level) {
    if (level <= 1) return 20;
    if (level == 2) return 40;
    return 60;
}

bool isDistractionState(FocusState state) {
    return state == FocusState::LOOKING_AWAY ||
           state == FocusState::SLEEPY_OR_SLUMPED ||
           state == FocusState::PHONE_IN_USE ||
           state == FocusState::PHONE_IN_PLAYING ||
           state == FocusState::AWAY ||
           state == FocusState::LOW_CONFIDENCE;
}

bool isPhoneState(FocusState state) {
    return state == FocusState::PHONE_IN_USE ||
           state == FocusState::PHONE_IN_LEARNING ||
           state == FocusState::PHONE_IN_PLAYING;
}

void applyFocusAccumulation(SessionInfo& s, int64_t now_ms, FocusState focus_state,
                           int64_t& focus_tracker_ms, int64_t& focus_accumulated_ms_ref) {
    if (deriveSessionPhase(s) != SessionPhase::FOCUSING) {
        focus_tracker_ms = 0;
        return;
    }

    if (focus_state == FocusState::FOCUSED) {
        if (focus_tracker_ms > 0) {
            const int64_t delta_ms = now_ms - focus_tracker_ms;
            if (delta_ms > 0) {
                s.focus_accumulated_ms += delta_ms;
                focus_accumulated_ms_ref += delta_ms;
            }
        }
        focus_tracker_ms = now_ms;
    } else {
        focus_tracker_ms = now_ms;
    }
}

FocusState focusStateForPhoneUsage(PhoneUsageState usage_state) {
    if (usage_state == PhoneUsageState::LEARNING) return FocusState::PHONE_IN_LEARNING;
    if (usage_state == PhoneUsageState::PLAYING) return FocusState::PHONE_IN_PLAYING;
    return FocusState::PHONE_IN_USE;
}

BehaviorContext makePhoneContext(const PhoneUsageSignal& sig) {
    BehaviorContext ctx;
    ctx.phone_signal = sig.active;
    ctx.vision_signal = false;
    if (sig.active) {
        ctx.phone_distraction_level = clampPhoneDistractionLevel(sig.distraction_level);
        ctx.phone_usage_state = static_cast<int>(sig.usage_state);
        ctx.risk_score = estimatePhoneRisk(sig.distraction_level);
        ctx.reason = "phone_sync";
    } else {
        ctx.risk_score = 0;
        ctx.phone_distraction_level = -1;
        ctx.phone_usage_state = -1;
        ctx.reason = "phone_sync_inactive";
    }
    return ctx;
}

BehaviorContext makeVisionContext(const FocusEvent& event, int64_t risk_score, int64_t frame_ts) {
    BehaviorContext ctx = {};
    ctx.risk_score = static_cast<int>(risk_score);
    ctx.vision_signal = event.vision_signal;
    ctx.phone_signal = event.phone_signal;
    ctx.phone_distraction_level = event.phone_distraction_level;
    ctx.phone_usage_state = event.phone_usage_state_raw;
    ctx.reason = event.reason.empty() ? "vision" : event.reason;
    if (frame_ts > 0) {
        ctx.reason += " @";
        ctx.reason += std::to_string(frame_ts);
    }
    return ctx;
}

#ifdef EDGEFOCUS_WITH_DEMO_HTTP
BehaviorContext makeDemoContext(const DemoOverride& demo) {
    BehaviorContext ctx = {};
    ctx.risk_score = std::clamp(demo.risk_score, 0, 100);
    ctx.vision_signal = true;
    ctx.phone_signal = demo.focus_state == FocusState::PHONE_IN_USE ||
                       demo.focus_state == FocusState::PHONE_IN_PLAYING ||
                       demo.focus_state == FocusState::PHONE_IN_LEARNING;
    if (!ctx.phone_signal) {
        ctx.phone_distraction_level = -1;
        ctx.phone_usage_state = -1;
        ctx.reason = demo.reason.empty() ? "demo_override" : demo.reason;
        return ctx;
    }
    ctx.phone_distraction_level = ctx.risk_score >= 90 ? 3 : (ctx.risk_score >= 60 ? 2 : 1);
    ctx.phone_usage_state = static_cast<int>(demo.focus_state == FocusState::PHONE_IN_LEARNING
                                               ? PhoneUsageState::LEARNING
                                               : demo.focus_state == FocusState::PHONE_IN_PLAYING
                                                     ? PhoneUsageState::PLAYING
                                                     : PhoneUsageState::IN_USE);
    ctx.reason = demo.reason.empty() ? "demo_override" : demo.reason;
    return ctx;
}
#endif

struct PrecheckGuidance {
    std::string distance_hint;
    std::string pose_hint;
    bool ready = false;
};

PrecheckGuidance makePrecheckGuidance(const VisionResult& result) {
    PrecheckGuidance guidance;

    if (!result.frame_quality.valid) {
        guidance.distance_hint = "camera_waiting";
        guidance.pose_hint = "keep_visible";
        return guidance;
    }
    if (result.frame_quality.level == FrameQualityLevel::DARK) {
        guidance.distance_hint = "lighting_too_dark";
        guidance.pose_hint = "increase_light";
        return guidance;
    }
    if (result.frame_quality.level == FrameQualityLevel::BRIGHT) {
        guidance.distance_hint = "lighting_too_bright";
        guidance.pose_hint = "reduce_backlight";
        return guidance;
    }
    if (result.frame_quality.level == FrameQualityLevel::BLURRY) {
        guidance.distance_hint = "image_blurry";
        guidance.pose_hint = "hold_still";
        return guidance;
    }
    if (result.frame_quality.level == FrameQualityLevel::PARTIAL) {
        guidance.distance_hint = "move_farther_or_center";
        guidance.pose_hint = "keep_head_in_frame";
        return guidance;
    }
    if (result.isAway() || !result.presence.valid || result.presence.confidence < 0.5f) {
        guidance.distance_hint = "move_closer";
        guidance.pose_hint = "sit_in_front_of_camera";
        return guidance;
    }
    if (result.face.valid && result.face.value == FaceState::NOT_FOUND) {
        guidance.distance_hint = "adjust_distance";
        guidance.pose_hint = "face_camera";
        return guidance;
    }
    if (result.head_pose.valid) {
        const auto& pose = result.head_pose.value;
        if (std::abs(pose.yaw) > 25.0f) {
            guidance.distance_hint = "distance_ok";
            guidance.pose_hint = pose.yaw > 0 ? "turn_head_left" : "turn_head_right";
            return guidance;
        }
        if (std::abs(pose.pitch) > 20.0f) {
            guidance.distance_hint = "distance_ok";
            guidance.pose_hint = pose.pitch > 0 ? "lower_camera_or_head" : "raise_camera_or_head";
            return guidance;
        }
    }

    guidance.distance_hint = "distance_ok";
    guidance.pose_hint = "pose_ok";
    guidance.ready = result.frame_quality.score >= 0.6f && result.isPresent();
    return guidance;
}
} // namespace

BehaviorEngine::BehaviorEngine(BoundedQueue<VisionResult>& vision_queue, SystemStateStore& store, SQLiteStore& sqlite_store)
    : Module("BehaviorEngine"), vision_queue_(vision_queue), store_(store), sqlite_store_(sqlite_store) {}

bool BehaviorEngine::init(AppContext& /*ctx*/) {
    LOGI("BehaviorEngine", "Initialized");
    return true;
}

void BehaviorEngine::runOnce(AppContext& /*ctx*/) {
    auto now_ms = monotonicMs();
    SystemSnapshot snap = store_.snapshot();

    // Consume phone signal from BleGattServer (via store buffer)
    PhoneUsageSignal phone_sig;
    if (store_.consumePhoneSignal(phone_sig)) {
        fsm_.injectPhoneSignal(phone_sig);
        auto context = makePhoneContext(phone_sig);
        store_.updateBehaviorContext(context);

        if (deriveSessionPhase(snap.session) == SessionPhase::FOCUSING) {
            auto current_snap = store_.snapshot();
            SessionInfo s = current_snap.session;
            if (phone_episode_session_id_ != s.session_id) {
                phone_episode_session_id_ = s.session_id;
                phone_episode_active_ = false;
            }
            const FocusState previous_state = current_snap.focus_state;
            if (phone_sig.active) {
                if (!phone_episode_active_) {
                    s.phone_distraction_count++;
                    phone_episode_active_ = true;
                }
                s.current_phone_distraction_level = clampPhoneDistractionLevel(phone_sig.distraction_level);
                store_.updateSessionIfCurrent(s);

                FocusState phone_state = focusStateForPhoneUsage(phone_sig.usage_state);
                if (!isDistractionState(previous_state) && isDistractionState(phone_state)) {
                    store_.incrementDistraction();
                }
                if (previous_state != phone_state) {
                    store_.updateFocusState(phone_state, phone_sig.timestamp_ms, "Phone sync signal");
                    nlohmann::json push_data = {
                        {"focus_state", static_cast<int>(phone_state)},
                        {"elapsed_ms", s.elapsed_ms},
                        {"distraction_count", s.distraction_count},
                        {"session_id", s.session_id},
                        {"calibration_completed", s.calibration_completed}
                    };
                    nlohmann::json push_event = {{"cmd", "push_state"}, {"data", push_data}};
                    store_.pushEvent(push_event.dump());
                }
            } else {
                phone_episode_active_ = false;
                s.current_phone_distraction_level = 0;
                store_.updateSessionIfCurrent(s);
                if (isPhoneState(previous_state)) {
                    store_.updateFocusState(FocusState::FOCUSED, phone_sig.timestamp_ms, "phone_sync_inactive");
                    nlohmann::json push_data = {
                        {"focus_state", static_cast<int>(FocusState::FOCUSED)},
                        {"elapsed_ms", s.elapsed_ms},
                        {"distraction_count", s.distraction_count},
                        {"session_id", s.session_id},
                        {"calibration_completed", s.calibration_completed}
                    };
                    nlohmann::json push_event = {{"cmd", "push_state"}, {"data", push_data}};
                    store_.pushEvent(push_event.dump());
                }
            }
        } else if (!phone_sig.active) {
            phone_episode_active_ = false;
        }
    }

    // --- Session phase routing ---
    const SessionPhase phase = deriveSessionPhase(snap.session);
    if (phase == SessionPhase::IDLE) {
        if (session_was_active_) {
            // Session end accounting is performed by the explicit end paths.
            // This branch only resets behavior-local state after another owner ends a session.
            fsm_.reset();
            while (vision_queue_.tryPop().has_value()) {}
            session_was_active_ = false;
            focus_accumulated_ms_ = 0;
            last_focus_update_ms_ = 0;
            stable_distance_hint_.clear();
            stable_pose_hint_.clear();
            stable_precheck_ready_ = false;
            pending_distance_hint_.clear();
            pending_pose_hint_.clear();
            pending_precheck_ready_ = false;
            pending_guidance_since_ms_ = 0;
            phone_episode_active_ = false;
            phone_episode_session_id_.clear();
            LOGI("BehaviorEngine", "Session ended, FSM reset");
        }
        // Block until session becomes active
        std::unique_lock<std::mutex> lock(store_.mutexForCV());
        store_.waitForSessionChange(lock, 1000);
        return;
    }

    session_was_active_ = true;

    if (phase == SessionPhase::PAUSED) {
        auto s = store_.snapshot().session;
        s.focus_accumulated_ms = std::max(s.focus_accumulated_ms, focus_accumulated_ms_);
        applyFocusAccumulation(s, now_ms, FocusState::AWAY, last_focus_update_ms_, focus_accumulated_ms_);
        store_.updateSessionIfCurrent(s);
        // Pause should freeze behavior progression: discard queued vision frames
        // and keep focus/distraction counters unchanged.
        while (vision_queue_.tryPop().has_value()) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return;
    }

    if (phase == SessionPhase::PRECHECK) {
        auto s = store_.snapshot().session;
        s.focus_accumulated_ms = std::max(s.focus_accumulated_ms, focus_accumulated_ms_);
        applyFocusAccumulation(s, now_ms, FocusState::AWAY, last_focus_update_ms_, focus_accumulated_ms_);
        store_.updateSessionIfCurrent(s);
        snap = store_.snapshot();
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
        if (snap.demo_override.precheck_guidance_override_enabled) {
            store_.updatePrecheckGuidance(snap.demo_override.distance_hint,
                                          snap.demo_override.pose_hint,
                                          snap.demo_override.precheck_ready);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return;
        }
#endif
        std::optional<VisionResult> latest;
        while (auto item = vision_queue_.tryPop()) {
            latest = *item;
        }
        if (latest.has_value()) {
            const auto& vision_result = latest.value();
            const bool guidance_needed = !vision_result.frame_quality.valid ||
                                         vision_result.frame_quality.score < 0.6f ||
                                         vision_result.frame_quality.level != FrameQualityLevel::GOOD;
            store_.updateFrameQuality(vision_result.frame_quality.score,
                                      vision_result.frame_quality.reason ? vision_result.frame_quality.reason : "",
                                      guidance_needed);
            auto guidance = makePrecheckGuidance(vision_result);
            const bool first_guidance = stable_distance_hint_.empty() && stable_pose_hint_.empty();
            const bool same_as_stable =
                guidance.distance_hint == stable_distance_hint_ &&
                guidance.pose_hint == stable_pose_hint_ &&
                guidance.ready == stable_precheck_ready_;
            if (first_guidance || same_as_stable) {
                stable_distance_hint_ = guidance.distance_hint;
                stable_pose_hint_ = guidance.pose_hint;
                stable_precheck_ready_ = guidance.ready;
                pending_guidance_since_ms_ = 0;
            } else {
                const bool same_as_pending =
                    guidance.distance_hint == pending_distance_hint_ &&
                    guidance.pose_hint == pending_pose_hint_ &&
                    guidance.ready == pending_precheck_ready_;
                if (!same_as_pending) {
                    pending_distance_hint_ = guidance.distance_hint;
                    pending_pose_hint_ = guidance.pose_hint;
                    pending_precheck_ready_ = guidance.ready;
                    pending_guidance_since_ms_ = now_ms;
                } else if (pending_guidance_since_ms_ > 0 &&
                           now_ms - pending_guidance_since_ms_ >= kPrecheckGuidanceDebounceMs) {
                    stable_distance_hint_ = pending_distance_hint_;
                    stable_pose_hint_ = pending_pose_hint_;
                    stable_precheck_ready_ = pending_precheck_ready_;
                    pending_guidance_since_ms_ = 0;
                }
            }
            store_.updatePrecheckGuidance(stable_distance_hint_, stable_pose_hint_,
                                          stable_precheck_ready_);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return;
    }

    // --- Update elapsed time using system clock (independent of vision data) ---
    {
        SessionInfo s = store_.snapshot().session;
        s.focus_accumulated_ms = std::max(s.focus_accumulated_ms, focus_accumulated_ms_);
        applyFocusAccumulation(s, now_ms, fsm_.getCurrentState(), last_focus_update_ms_, focus_accumulated_ms_);
        if (s.active && s.start_ms > 0) {
            s.elapsed_ms = now_ms - s.start_ms;
            if (s.target_duration_ms > 0) {
                s.remaining_ms = s.target_duration_ms - s.elapsed_ms;
                if (s.remaining_ms <= 0) {
                    s.remaining_ms = 0;
                    LOGI("BehaviorEngine", "Target duration reached, auto-terminating session");
                    store_.updateSessionIfCurrent(s);
                    SessionController controller(store_, sqlite_store_);
                    auto ended = controller.endSession("timer_completed");
                    const SessionReport& report = ended.report;

                    if (ended.success && !report.session_id.empty()) {
                        nlohmann::json push_data = {
                            {"elapsed_ms", report.total_duration * 1000},
                            {"target_duration_ms", ended.target_duration_ms},
                            {"distraction_count", report.distraction_count},
                            {"reminder_count", report.reminder_count},
                            {"phone_distraction_count", report.phone_distraction_count},
                            {"session_id", report.session_id}
                        };
                        nlohmann::json push_event = {{"cmd", "push_timer_complete"}, {"data", push_data}};
                        store_.pushEvent(push_event.dump());
                    }
                    session_was_active_ = false;
                    focus_accumulated_ms_ = 0;
                    last_focus_update_ms_ = 0;
                    phone_episode_active_ = false;
                    phone_episode_session_id_.clear();

                    return;
                }
            }
            store_.updateSessionIfCurrent(s);
        }
        store_.updateSessionIfCurrent(s);
    }

    snap = store_.snapshot();
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    if (snap.demo_override.focus_override_enabled) {
        const FocusState old_state = snap.focus_state;
        const FocusState new_state = snap.demo_override.focus_state;
        if (!isDistractionState(old_state) && isDistractionState(new_state)) {
            store_.incrementDistraction();
        }
        store_.updateBehaviorContext(makeDemoContext(snap.demo_override));
        if (old_state != new_state || snap.focus_reason != snap.demo_override.reason) {
            store_.updateFocusState(new_state, now_ms,
                                    snap.demo_override.reason.empty() ? "demo_override"
                                                                      : snap.demo_override.reason);
            auto current_snap = store_.snapshot();
            nlohmann::json push_data = {
                {"focus_state", static_cast<int>(new_state)},
                {"elapsed_ms", current_snap.session.elapsed_ms},
                {"distraction_count", current_snap.session.distraction_count},
                {"session_id", current_snap.session.session_id},
                {"calibration_completed", current_snap.session.calibration_completed}
            };
            nlohmann::json push_event = {{"cmd", "push_state"}, {"data", push_data}};
            store_.pushEvent(push_event.dump());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return;
    }
#endif

    // --- ACTIVE: process vision results ---
    auto result_opt = vision_queue_.tryPop();
    if (!result_opt.has_value()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return;
    }

    const auto& vision_result = result_opt.value();
    const bool guidance_needed = !vision_result.frame_quality.valid ||
                                 vision_result.frame_quality.score < 0.6f ||
                                 vision_result.frame_quality.level != FrameQualityLevel::GOOD;
    store_.updateFrameQuality(vision_result.frame_quality.score,
                              vision_result.frame_quality.reason ? vision_result.frame_quality.reason : "",
                              guidance_needed);
    FocusState old_state = fsm_.getCurrentState();
    fsm_.processFrame(vision_result);
    FocusState new_state = fsm_.getCurrentState();
    FocusEvent event = fsm_.getLastEvent();
    BehaviorContext context = makeVisionContext(event, fsm_.getRiskScore(), vision_result.timestamp_ms);
    store_.updateBehaviorContext(context);

    // --- State change handling ---
    if (old_state != new_state) {
        if (!isDistractionState(old_state) && isDistractionState(new_state)) {
            store_.incrementDistraction();
        }

        std::string reason = "State transition";
        store_.updateFocusState(new_state, vision_result.timestamp_ms, reason);

        // Push state change event to phone
        {
            auto current_snap = store_.snapshot();
            nlohmann::json push_data = {
                {"focus_state", static_cast<int>(new_state)},
                {"elapsed_ms", current_snap.session.elapsed_ms},
                {"distraction_count", current_snap.session.distraction_count},
                {"session_id", current_snap.session.session_id},
                {"calibration_completed", current_snap.session.calibration_completed}
            };
            nlohmann::json push_event = {{"cmd", "push_state"}, {"data", push_data}};
            store_.pushEvent(push_event.dump());
        }

        // Track away duration for auto-timeout
        SessionInfo s = store_.snapshot().session;
        if (new_state == FocusState::AWAY) {
            if (s.away_since_ms == 0) {
                s.away_since_ms = vision_result.timestamp_ms;
                store_.updateSessionIfCurrent(s);
            }
        } else {
            // Reset away timer when no longer away
            if (s.away_since_ms != 0) {
                s.away_since_ms = 0;
                store_.updateSessionIfCurrent(s);
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
        int64_t now_ms = monotonicMs();
        snap = store_.snapshot();
        if (snap.session.away_since_ms > 0 && snap.session.active) {
            int64_t away_duration = now_ms - snap.session.away_since_ms;
            int64_t timeout_ms = snap.session.away_timeout_ms;
            if (timeout_ms <= 0) timeout_ms = 30 * 60 * 1000;

            if (away_duration >= timeout_ms) {
                LOGI("BehaviorEngine", "Away for %lld ms, auto-terminating session",
                     (long long)away_duration);
                SessionInfo s = snap.session;
                s.focus_accumulated_ms = std::max(s.focus_accumulated_ms, focus_accumulated_ms_);
                store_.updateSessionIfCurrent(s);
                SessionController controller(store_, sqlite_store_);
                auto ended = controller.endSession("away_timeout");
                const SessionReport& report = ended.report;

                if (ended.success && !report.session_id.empty()) {
                    nlohmann::json push_data = {
                        {"total_duration", report.total_duration},
                        {"focus_duration", report.focus_duration},
                        {"distraction_count", report.distraction_count},
                        {"reminder_count", report.reminder_count},
                        {"phone_distraction_count", report.phone_distraction_count},
                        {"session_id", report.session_id}
                    };
                    nlohmann::json push_event = {{"cmd", "push_session_end"}, {"data", push_data}};
                    store_.pushEvent(push_event.dump());
                }
                session_was_active_ = false;
                focus_accumulated_ms_ = 0;
                last_focus_update_ms_ = 0;
                phone_episode_active_ = false;
                phone_episode_session_id_.clear();
                return;
            }
        }
    }

    // --- Track focus time accumulation ---
    {
        if (new_state == FocusState::FOCUSED) {
            if (last_focus_update_ms_ > 0) {
                focus_accumulated_ms_ += now_ms - last_focus_update_ms_;
            }
            last_focus_update_ms_ = now_ms;
        } else {
            last_focus_update_ms_ = now_ms;
        }
        SessionInfo focused_session = store_.snapshot().session;
        focused_session.focus_accumulated_ms =
            std::max(focused_session.focus_accumulated_ms, focus_accumulated_ms_);
        store_.updateSessionIfCurrent(focused_session);
    }
}

void BehaviorEngine::shutdown(AppContext& /*ctx*/) {
    LOGI("BehaviorEngine", "Shutdown");
}

} // namespace edgefocus
