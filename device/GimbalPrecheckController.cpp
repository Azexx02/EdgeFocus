#include "device/GimbalPrecheckController.h"
#include "app/Logger.h"
#include "device/GimbalVisualEvidence.h"
#include "platform/gimbal/GimbalProtocol.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace edgefocus::gimbal {

std::optional<Waypoint> GimbalPrecheckController::resolveWaypointLocked(
    const Waypoint& waypoint) const {
    if (!config_.relative_to_alignment) return waypoint;
    if (!alignment_reference_) return std::nullopt;
    return Waypoint{offsetAngle(alignment_reference_->yaw_deg, waypoint.yaw_deg),
                    offsetAngle(alignment_reference_->pitch_deg, waypoint.pitch_deg)};
}

bool PrecheckConfig::verified() const noexcept {
    const auto waypoints_valid = [](const std::vector<Waypoint>& waypoints) {
        return std::all_of(waypoints.begin(), waypoints.end(),
                           [](const Waypoint& waypoint) {
                               return std::isfinite(waypoint.yaw_deg) &&
                                      std::isfinite(waypoint.pitch_deg) &&
                                      waypoint.deadline_ms > 0;
                           });
    };
    return center.has_value() && !search_waypoints.empty() &&
           waypoints_valid(search_waypoints) &&
           waypoints_valid(recovery_waypoints) && nudge_step_deg > 0.0 &&
           initial_observe_ms >= 0 && waypoint_period_ms > 0 && stop_queueing_ms > 0 &&
           hard_deadline_ms > stop_queueing_ms && subject_observations >= 2 &&
           candidate_centering_ms > 0 && framing_window_ms >= 750 &&
           framing_pass_score > 0.0 && framing_pass_score <= 1.0;
}

const char* precheckStateName(PrecheckState state) noexcept {
    switch (state) {
        case PrecheckState::Inactive: return "inactive";
        case PrecheckState::Searching: return "searching";
        case PrecheckState::Centering: return "centering";
        case PrecheckState::Framing: return "framing";
        case PrecheckState::Manual: return "manual";
        case PrecheckState::Holding: return "holding";
        case PrecheckState::Degraded: return "degraded";
    }
    return "inactive";
}

GimbalPrecheckController::GimbalPrecheckController(
    IGimbalSessionHardware& hardware, PrecheckConfig config,
    GimbalVisualTracker* centering_tracker, Clock clock)
    : hardware_(hardware),
      config_(std::move(config)),
      centering_tracker_(centering_tracker),
      clock_(std::move(clock)) {}

CommandResult GimbalPrecheckController::rejected(std::string error) {
    CommandResult result;
    result.error = std::move(error);
    return result;
}

bool GimbalPrecheckController::start(
    const std::string& session_id, std::int64_t now_ms, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_id.empty()) {
        if (error) *error = "session_id_required";
        return false;
    }
    status_.session_id = session_id;
    if (!config_.verified()) {
        if (error) *error = "gimbal_precheck_config_unverified";
        degradeLocked("gimbal_precheck_config_unverified");
        return false;
    }
    auto hardware = hardware_.status();
    if (hardware.state == State::Disabled && !hardware.fault_active) {
        const auto aligned = hardware_.enableAndAlign(10000);
        if (!aligned.success) {
            const auto detail = aligned.error.empty() ? "gimbal_align_failed" : aligned.error;
            if (error) *error = detail;
            degradeLocked(detail);
            const auto failure_ms = clock_ ? clock_() : now_ms;
            next_restore_attempt_ms_ = failure_ms + 5000;
            return false;
        }
        hardware = hardware_.status();
    }
    if (hardware.state != State::Ready && hardware.state != State::Holding) {
        const auto detail = std::string("gimbal_not_ready:") + stateName(hardware.state);
        if (error) *error = detail;
        degradeLocked(detail);
        return false;
    }
    if (config_.relative_to_alignment) {
        if (!hardware.yaw_deg || !hardware.pitch_deg) {
            if (error) *error = "gimbal_alignment_reference_unknown";
            degradeLocked("gimbal_alignment_reference_unknown");
            return false;
        }
        alignment_reference_ = Waypoint{*hardware.yaw_deg, *hardware.pitch_deg};
    } else {
        alignment_reference_.reset();
    }
    ++status_.generation;
    status_.state = PrecheckState::Searching;
    status_.session_id = session_id;
    status_.waypoint_index = 0;
    status_.subject_found = false;
    status_.manual_lease = false;
    status_.detail = "observing_current_view";
    // Hardware alignment may block for tens of seconds on the real G431.
    // The approved 75-second budget applies to automatic visual search, not
    // to the preceding link/alignment handshake.
    const auto search_started_ms = clock_ ? clock_() : now_ms;
    started_ms_ = search_started_ms;
    next_waypoint_ms_ = search_started_ms + config_.initial_observe_ms;
    first_subject_ms_ = 0;
    last_subject_evidence_ms_ = 0;
    subject_count_ = 0;
    last_command_sequence_ = 0;
    next_restore_attempt_ms_ = 0;
    primary_waypoint_index_ = 0;
    recovery_waypoint_index_ = 0;
    visited_waypoints_ = 0;
    recovery_started_ = false;
    active_waypoint_.reset();
    frozen_subject_box_.reset();
    cooldown_subject_box_.reset();
    cooldown_until_ms_ = 0;
    centering_deadline_ms_ = 0;
    framing_started_ms_ = 0;
    last_visual_frame_id_ = 0;
    missing_subject_frames_ = 0;
    centered_frames_ = 0;
    framing_scores_.clear();
    if (centering_tracker_) centering_tracker_->reset();
    LOGI("GimbalPrecheckController",
         "PRECHECK search started session=%s hard_deadline_ms=%lld primary=%zu recovery=%zu",
         status_.session_id.c_str(),
         static_cast<long long>(config_.hard_deadline_ms),
         config_.search_waypoints.size(), config_.recovery_waypoints.size());
    if (error) error->clear();
    return true;
}

void GimbalPrecheckController::degradeLocked(std::string detail) noexcept {
    status_.state = PrecheckState::Degraded;
    status_.manual_lease = false;
    status_.detail = std::move(detail);
}

void GimbalPrecheckController::finishSearchLocked(
    bool subject_found, std::int64_t /*now_ms*/) {
    CommandResult result;
    if (subject_found) {
        result = hardware_.hold(1000);
    } else if (config_.center) {
        const auto center = resolveWaypointLocked(*config_.center);
        if (!center) {
            degradeLocked("gimbal_alignment_reference_unknown");
            return;
        }
        result = hardware_.moveToVerified(
            center->yaw_deg, center->pitch_deg, 8000, 1.5);
        if (result.success) result = hardware_.hold(1000);
    }
    if (!result.success) {
        (void)hardware_.stopAndDisable(1000);
        degradeLocked(result.error.empty() ? "gimbal_search_finish_failed" : result.error);
        return;
    }
    status_.state = PrecheckState::Manual;
    status_.subject_found = subject_found;
    status_.manual_lease = true;
    status_.detail = subject_found ? "subject_found_manual"
                                   : "search_timeout_manual";
    frozen_subject_box_.reset();
    framing_scores_.clear();
    if (centering_tracker_) centering_tracker_->reset();
    LOGI("GimbalPrecheckController",
         "PRECHECK search finished subject_found=%d manual_lease=1 waypoint=%zu",
         subject_found ? 1 : 0, status_.waypoint_index);
}

bool GimbalPrecheckController::visualInCooldownLocked(
    std::int64_t now_ms, const session::VisualSnapshot& visual) const {
    return cooldown_subject_box_ && now_ms < cooldown_until_ms_ &&
           associateVisualSubject(visual, *cooldown_subject_box_).accepted;
}

void GimbalPrecheckController::advanceSearchLocked(std::int64_t now_ms) {
    if (now_ms - started_ms_ >= config_.stop_queueing_ms) {
        finishSearchLocked(false, now_ms);
        return;
    }

    Waypoint offset;
    if (primary_waypoint_index_ < config_.search_waypoints.size()) {
        offset = config_.search_waypoints[primary_waypoint_index_++];
        status_.detail = "primary_search";
    } else if (recovery_waypoint_index_ < config_.recovery_waypoints.size()) {
        recovery_started_ = true;
        offset = config_.recovery_waypoints[recovery_waypoint_index_++];
        status_.detail = "recovery_search";
    } else {
        finishSearchLocked(false, now_ms);
        return;
    }

    const auto waypoint = resolveWaypointLocked(offset);
    if (!waypoint) {
        (void)hardware_.stopAndDisable(1000);
        degradeLocked("gimbal_alignment_reference_unknown");
        return;
    }
    const auto moved = hardware_.moveTo(
        waypoint->yaw_deg, waypoint->pitch_deg, 2000);
    if (!moved.success) {
        (void)hardware_.stopAndDisable(1000);
        degradeLocked(moved.error.empty() ? "gimbal_waypoint_failed"
                                          : moved.error);
        return;
    }
    active_waypoint_ = offset;
    status_.waypoint_index = ++visited_waypoints_;
    first_subject_ms_ = 0;
    last_subject_evidence_ms_ = 0;
    subject_count_ = 0;
    next_waypoint_ms_ = now_ms + std::max<std::int64_t>(
        300, offset.deadline_ms);
}

bool GimbalPrecheckController::beginCandidateLocked(
    std::int64_t now_ms, const session::VisualSnapshot& visual) {
    if (!visual.subject_box) return false;
    const auto frozen = hardware_.freezeAtCurrent(2000);
    if (!frozen.success) {
        (void)hardware_.stopAndDisable(1000);
        degradeLocked(frozen.error.empty() ? "gimbal_candidate_freeze_failed"
                                           : frozen.error);
        return false;
    }
    if (!centering_tracker_) {
        finishSearchLocked(true, now_ms);
        return true;
    }
    LOGI("GimbalPrecheckController",
         "Pose candidate acquired frame=%llu; freezing and centering",
         static_cast<unsigned long long>(visual.frame_id));
    frozen_subject_box_ = *visual.subject_box;
    status_.state = PrecheckState::Centering;
    status_.detail = "candidate_centering";
    centering_deadline_ms_ = now_ms + config_.candidate_centering_ms;
    missing_subject_frames_ = 0;
    centered_frames_ = 0;
    centering_tracker_->reset();
    return true;
}

void GimbalPrecheckController::resumeSearchLocked(
    std::int64_t now_ms, std::string detail) {
    if (frozen_subject_box_) {
        cooldown_subject_box_ = frozen_subject_box_;
        cooldown_until_ms_ = now_ms + 3000;
    }
    if (centering_tracker_) centering_tracker_->reset();
    frozen_subject_box_.reset();
    framing_scores_.clear();
    missing_subject_frames_ = 0;
    centered_frames_ = 0;
    status_.state = PrecheckState::Searching;
    status_.detail = std::move(detail);
    if (active_waypoint_) {
        const auto waypoint = resolveWaypointLocked(*active_waypoint_);
        if (!waypoint) {
            (void)hardware_.stopAndDisable(1000);
            degradeLocked("gimbal_alignment_reference_unknown");
            return;
        }
        const auto moved = hardware_.moveTo(
            waypoint->yaw_deg, waypoint->pitch_deg, 2000);
        if (!moved.success) {
            (void)hardware_.stopAndDisable(1000);
            degradeLocked(moved.error.empty() ? "gimbal_search_resume_failed"
                                              : moved.error);
            return;
        }
    }
    next_waypoint_ms_ = now_ms + 500;
}

void GimbalPrecheckController::beginFramingLocked(std::int64_t now_ms) {
    const auto frozen = hardware_.freezeAtCurrent(2000);
    if (!frozen.success) {
        (void)hardware_.stopAndDisable(1000);
        degradeLocked(frozen.error.empty() ? "gimbal_centered_freeze_failed"
                                           : frozen.error);
        return;
    }
    status_.state = PrecheckState::Framing;
    status_.detail = "framing_confirmation";
    framing_started_ms_ = now_ms;
    framing_scores_.clear();
    if (centering_tracker_) centering_tracker_->reset();
    LOGI("GimbalPrecheckController", "Candidate centered; confirming framing");
}

void GimbalPrecheckController::observeCenteringLocked(
    std::int64_t now_ms, const session::VisualSnapshot& visual) {
    if (!frozen_subject_box_) {
        resumeSearchLocked(now_ms, recovery_started_ ? "recovery_search"
                                                     : "primary_search");
        return;
    }
    const auto association = associateVisualSubject(visual, *frozen_subject_box_);
    if (!association.accepted || !association.subject_box) {
        if (++missing_subject_frames_ >= 2) {
            resumeSearchLocked(now_ms, recovery_started_ ? "recovery_search"
                                                         : "primary_search");
        }
        return;
    }
    missing_subject_frames_ = 0;
    frozen_subject_box_ = *association.subject_box;
    auto tracking = centering_tracker_->update(now_ms, visual);
    if (!tracking.success) {
        (void)hardware_.stopAndDisable(1000);
        degradeLocked(tracking.error.empty() ? "gimbal_candidate_center_failed"
                                             : tracking.error);
        return;
    }
    if (tracking.error == "target_in_deadband") {
        ++centered_frames_;
    } else {
        centered_frames_ = 0;
    }
    if (centered_frames_ >= 2 || now_ms >= centering_deadline_ms_) {
        beginFramingLocked(now_ms);
    }
}

void GimbalPrecheckController::observeFramingLocked(
    std::int64_t now_ms, const session::VisualSnapshot& visual) {
    if (now_ms - framing_started_ms_ > config_.framing_window_ms) {
        resumeSearchLocked(now_ms, recovery_started_ ? "recovery_search"
                                                     : "primary_search");
        return;
    }
    double score = 0.0;
    if (frozen_subject_box_) {
        const auto association = associateVisualSubject(
            visual, *frozen_subject_box_);
        if (association.accepted && association.subject_box) {
            frozen_subject_box_ = *association.subject_box;
            const auto framing = evaluateVisualFraming(visual);
            if (framing.eligible) score = framing.quality;
        }
    }
    framing_scores_.push_back(score);
    if (framing_scores_.size() < 3) return;
    const auto passing = static_cast<unsigned>(std::count_if(
        framing_scores_.begin(), framing_scores_.end(),
        [&](double quality) { return quality >= config_.framing_pass_score; }));
    if (passing >= 2) {
        finishSearchLocked(true, now_ms);
    } else {
        resumeSearchLocked(now_ms, recovery_started_ ? "recovery_search"
                                                     : "primary_search");
    }
}

void GimbalPrecheckController::observe(
    std::int64_t now_ms, const session::VisualSnapshot& visual) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != PrecheckState::Searching &&
        status_.state != PrecheckState::Centering &&
        status_.state != PrecheckState::Framing) {
        return;
    }

    const auto hardware = hardware_.status();
    if (hardware.state != State::Ready && hardware.state != State::Holding &&
        hardware.state != State::Moving) {
        degradeLocked(std::string("gimbal_runtime_unhealthy:") +
                      stateName(hardware.state));
        return;
    }
    if (now_ms - started_ms_ >= config_.hard_deadline_ms) {
        finishSearchLocked(false, now_ms);
        return;
    }

    const bool new_visual = visual.frame_id != 0 &&
                            visual.frame_id > last_visual_frame_id_ &&
                            isFreshVisual(visual, now_ms,
                                          config_.visual_fresh_ms);
    if (new_visual) {
        last_visual_frame_id_ = visual.frame_id;
        if (status_.state == PrecheckState::Centering) {
            observeCenteringLocked(now_ms, visual);
            return;
        }
        if (status_.state == PrecheckState::Framing) {
            observeFramingLocked(now_ms, visual);
            return;
        }
        if (isPoseQuickTrigger(visual, now_ms, config_.visual_fresh_ms) &&
            !visualInCooldownLocked(now_ms, visual)) {
            (void)beginCandidateLocked(now_ms, visual);
            return;
        }
    }

    if (status_.state == PrecheckState::Searching &&
        now_ms >= next_waypoint_ms_) {
        advanceSearchLocked(now_ms);
    }
}

bool GimbalPrecheckController::restoreLocked(
    session::Phase phase, std::int64_t now_ms) {
    const auto hardware = hardware_.status();
    const bool safe_disabled = hardware.state == State::Disabled &&
        (hardware.recoverable || hardware.fault_latched_reason.empty());
    if (!safe_disabled || now_ms < next_restore_attempt_ms_) return false;
    next_restore_attempt_ms_ = now_ms + 5000;
    const auto aligned = hardware_.enableAndAlign(10000);
    if (!aligned.success) {
        degradeLocked(aligned.error.empty() ? "gimbal_realign_failed" : aligned.error);
        return false;
    }
    const auto restored = hardware_.status();
    if (config_.relative_to_alignment) {
        if (!restored.yaw_deg || !restored.pitch_deg) {
            degradeLocked("gimbal_alignment_reference_unknown");
            return false;
        }
        alignment_reference_ = Waypoint{*restored.yaw_deg, *restored.pitch_deg};
    }
    ++status_.generation;
    next_restore_attempt_ms_ = 0;
    status_.manual_lease = false;
    status_.subject_found = false;
    status_.detail = "gimbal_recovered";
    // Recovery establishes a new physical reference.  Any target accumulated
    // before the fault belongs to the old reference and must not be reused.
    if (centering_tracker_) centering_tracker_->reset();
    if (phase == session::Phase::Precheck) {
        status_.state = PrecheckState::Searching;
        status_.waypoint_index = 0;
        status_.detail = "observing_current_view";
        started_ms_ = now_ms;
        next_waypoint_ms_ = now_ms + config_.initial_observe_ms;
        first_subject_ms_ = 0;
        last_subject_evidence_ms_ = 0;
        subject_count_ = 0;
        primary_waypoint_index_ = 0;
        recovery_waypoint_index_ = 0;
        visited_waypoints_ = 0;
        recovery_started_ = false;
        active_waypoint_.reset();
        frozen_subject_box_.reset();
        cooldown_subject_box_.reset();
        last_visual_frame_id_ = 0;
        framing_scores_.clear();
        return true;
    }
    const auto held = hardware_.hold(1000);
    if (!held.success) {
        degradeLocked(held.error.empty() ? "gimbal_recovered_hold_failed" : held.error);
        return false;
    }
    status_.state = PrecheckState::Holding;
    return true;
}

void GimbalPrecheckController::maintain(
    session::Phase phase, std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == PrecheckState::Inactive) return;
    const auto hardware = hardware_.status();
    if (hardware.state == State::Fault || hardware.state == State::RecoveryCheck) {
        degradeLocked(hardware.fault_latched_reason.empty()
                          ? std::string("gimbal_runtime_fault:") + stateName(hardware.state)
                          : hardware.fault_latched_reason);
        return;
    }
    if (status_.state == PrecheckState::Degraded) {
        (void)restoreLocked(phase, now_ms);
        return;
    }
    if (hardware.state != State::Ready && hardware.state != State::Holding &&
        hardware.state != State::Moving) {
        degradeLocked(std::string("gimbal_runtime_unhealthy:") + stateName(hardware.state));
    }
}

bool GimbalPrecheckController::validateManualCommandLocked(
    const std::string& session_id, std::uint64_t sequence, CommandResult& result) {
    if (session_id.empty() || session_id != status_.session_id) {
        result = rejected("session_mismatch");
        return false;
    }
    if (!status_.manual_lease || status_.state != PrecheckState::Manual) {
        result = rejected("manual_lease_unavailable");
        return false;
    }
    if (sequence == 0 || sequence <= last_command_sequence_) {
        result = rejected("stale_command_sequence");
        return false;
    }
    last_command_sequence_ = sequence;
    return true;
}

CommandResult GimbalPrecheckController::skipAuto(
    const std::string& session_id, std::uint64_t sequence, std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_id != status_.session_id) return rejected("session_mismatch");
    if (status_.state != PrecheckState::Searching &&
        status_.state != PrecheckState::Centering &&
        status_.state != PrecheckState::Framing &&
        status_.state != PrecheckState::Holding) {
        return rejected("automatic_search_not_active");
    }
    if (sequence == 0 || sequence <= last_command_sequence_) return rejected("stale_command_sequence");
    last_command_sequence_ = sequence;
    ++status_.generation;
    auto result = hardware_.hold(1000);
    if (!result.success) {
        (void)hardware_.stopAndDisable(1000);
        degradeLocked(result.error.empty() ? "skip_stop_failed" : result.error);
        return result;
    }
    status_.state = PrecheckState::Manual;
    status_.manual_lease = true;
    status_.detail = "automatic_search_skipped";
    frozen_subject_box_.reset();
    framing_scores_.clear();
    if (centering_tracker_) centering_tracker_->reset();
    return result;
}

CommandResult GimbalPrecheckController::nudge(
    const std::string& session_id, std::uint64_t sequence,
    NudgeDirection direction, std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(session_id, sequence, result)) return result;
    const auto hardware = hardware_.status();
    if (!hardware.yaw_deg || !hardware.pitch_deg) return rejected("gimbal_angle_unknown");
    double yaw = *hardware.yaw_deg;
    double pitch = *hardware.pitch_deg;
    if (direction == NudgeDirection::Left) {
        yaw = offsetAngle(yaw, -config_.nudge_step_deg);
    }
    if (direction == NudgeDirection::Right) {
        yaw = offsetAngle(yaw, config_.nudge_step_deg);
    }
    if (direction == NudgeDirection::Up) {
        pitch = offsetAngle(pitch, config_.nudge_step_deg);
    }
    if (direction == NudgeDirection::Down) {
        pitch = offsetAngle(pitch, -config_.nudge_step_deg);
    }
    result = hardware_.moveToVerified(yaw, pitch, 5000, 1.5);
    // Reaching the configured safe window is an expected user-input boundary,
    // not a hardware failure. Keep the manual lease so the user can move back
    // in the opposite direction; real motion/UART failures still degrade safely.
    if (!result.success && result.error != "target_out_of_bounds") {
        degradeLocked(result.error.empty() ? "gimbal_nudge_failed" : result.error);
    }
    return result;
}

CommandResult GimbalPrecheckController::center(
    const std::string& session_id, std::uint64_t sequence, std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(session_id, sequence, result)) return result;
    if (!config_.center) return rejected("gimbal_center_unverified");
    const auto center = resolveWaypointLocked(*config_.center);
    if (!center) return rejected("gimbal_alignment_reference_unknown");
    result = hardware_.moveToVerified(
        center->yaw_deg, center->pitch_deg, 8000, 1.5);
    if (!result.success) degradeLocked(result.error.empty() ? "gimbal_center_failed" : result.error);
    return result;
}

CommandResult GimbalPrecheckController::stopMotion(
    const std::string& session_id, std::uint64_t sequence, std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(session_id, sequence, result)) return result;
    result = hardware_.hold(1000);
    if (!result.success) degradeLocked(result.error.empty() ? "gimbal_stop_failed" : result.error);
    return result;
}

CommandResult GimbalPrecheckController::confirmPosition(
    const std::string& session_id, std::uint64_t sequence, std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(session_id, sequence, result)) return result;
    result = hardware_.hold(1000);
    if (!result.success) {
        degradeLocked(result.error.empty() ? "gimbal_hold_failed" : result.error);
        return result;
    }
    status_.state = PrecheckState::Holding;
    status_.manual_lease = false;
    status_.detail = "position_confirmed";
    return result;
}

CommandResult GimbalPrecheckController::realign(
    const std::string& session_id, std::uint64_t sequence, std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_id != status_.session_id) return rejected("session_mismatch");
    if (sequence == 0 || sequence <= last_command_sequence_) return rejected("stale_command_sequence");
    const auto hardware = hardware_.status();
    if (hardware.state != State::Disabled || !hardware.recoverable) {
        return rejected("gimbal_not_recoverable");
    }
    last_command_sequence_ = sequence;
    auto result = hardware_.enableAndAlign(10000);
    if (!result.success) {
        degradeLocked(result.error.empty() ? "gimbal_realign_failed" : result.error);
        return result;
    }
    const auto aligned = hardware_.status();
    if (config_.relative_to_alignment) {
        if (!aligned.yaw_deg || !aligned.pitch_deg) {
            degradeLocked("gimbal_alignment_reference_unknown");
            return rejected("gimbal_alignment_reference_unknown");
        }
        alignment_reference_ = Waypoint{*aligned.yaw_deg, *aligned.pitch_deg};
    }
    status_.state = PrecheckState::Manual;
    status_.manual_lease = true;
    status_.detail = "realigned_manual_control";
    frozen_subject_box_.reset();
    framing_scores_.clear();
    if (centering_tracker_) centering_tracker_->reset();
    return result;
}

void GimbalPrecheckController::holdForSession(std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == PrecheckState::Inactive || status_.state == PrecheckState::Degraded) return;
    const auto result = hardware_.hold(1000);
    if (!result.success) {
        degradeLocked(result.error.empty() ? "gimbal_hold_failed" : result.error);
        return;
    }
    status_.state = PrecheckState::Holding;
    status_.manual_lease = false;
    if (centering_tracker_) centering_tracker_->reset();
}

void GimbalPrecheckController::stop() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == PrecheckState::Inactive) return;
    const auto next_generation = status_.generation + 1;
    const auto hardware = hardware_.status();
    bool center_attempted = false;
    bool center_succeeded = false;
    if (alignment_reference_ &&
        (hardware.state == State::Ready || hardware.state == State::Holding ||
         hardware.state == State::Moving) && !hardware.fault_active) {
        center_attempted = true;
        center_succeeded = hardware_.moveToVerified(
            alignment_reference_->yaw_deg, alignment_reference_->pitch_deg,
            8000, 1.5).success;
    }
    const auto stopped = hardware_.stopAndDisable(1000);
    const auto final_hardware = hardware_.status();
    LOGI("GimbalPrecheckController",
         "Session gimbal stop center_attempted=%d center_ok=%d stop_ok=%d final_state=%s fault=%s",
         center_attempted ? 1 : 0, center_succeeded ? 1 : 0,
         stopped.success ? 1 : 0, stateName(final_hardware.state),
         final_hardware.fault_latched_reason.empty()
             ? "clear"
             : final_hardware.fault_latched_reason.c_str());
    if (centering_tracker_) centering_tracker_->reset();
    status_ = {};
    alignment_reference_.reset();
    status_.generation = next_generation;
}

PrecheckStatus GimbalPrecheckController::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

Status GimbalPrecheckController::snapshotHardware() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hardware_.status();
}

}  // namespace edgefocus::gimbal
