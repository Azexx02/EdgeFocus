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
           waypoints_valid(recovery_waypoints) && normal_nudge_step_deg >= 1.0 &&
           normal_nudge_step_deg <= 90.0 && fine_nudge_step_deg >= 1.0 &&
           fine_nudge_step_deg <= normal_nudge_step_deg &&
           manual_boundary_deg > 0.0 && manual_boundary_deg < 180.0 &&
           boundary_settle_tolerance_deg > 0.0 &&
           boundary_settle_tolerance_deg <= 10.0 &&
           boundary_permission_ttl_ms >= 1000 &&
           initial_observe_ms >= 0 && waypoint_period_ms > 0 && stop_queueing_ms > 0 &&
           hard_deadline_ms > stop_queueing_ms && hard_deadline_ms <= 24000 &&
           subject_observations >= 2 && subject_observations <= 3 &&
           subject_stable_ms >= 250 && subject_stable_ms <= 500 &&
           candidate_centering_ms > 0 && framing_window_ms >= 750 &&
           framing_pass_score > 0.0 && framing_pass_score <= 1.0 &&
           settle_buffer_ms >= 150 && settle_buffer_ms <= 300 &&
           manual_lease_ttl_ms >= 5000 && !timing_source.empty();
}

const char* precheckStateName(PrecheckState state) noexcept {
    switch (state) {
        case PrecheckState::Inactive: return "inactive";
        case PrecheckState::MaintenanceRequired: return "maintenance_required";
        case PrecheckState::Searching: return "searching";
        case PrecheckState::Centering: return "centering";
        case PrecheckState::Framing: return "framing";
        case PrecheckState::ManualAvailable: return "manual_available";
        case PrecheckState::Manual: return "manual";
        case PrecheckState::Holding: return "holding";
        case PrecheckState::Released: return "released";
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

bool GimbalPrecheckController::isMaintenanceFailureLocked(
    const CommandResult& result, std::string& detail) const {
    const auto code = result.error_code.empty() ? result.error : result.error_code;
    if (code.find("CAL") != std::string::npos) {
        detail = "calibration_required";
        return true;
    }
    if (code.find("ORIGIN") != std::string::npos) {
        detail = "origin_required";
        return true;
    }
    if (code.find("CURRENT") != std::string::npos) {
        detail = "current_protection_required";
        return true;
    }
    return false;
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
            std::string maintenance_detail;
            if (isMaintenanceFailureLocked(aligned, maintenance_detail)) {
                status_.state = PrecheckState::MaintenanceRequired;
                status_.maintenance_required = true;
                status_.detail = maintenance_detail;
            } else {
                degradeLocked(detail);
            }
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
    if (!hardware.snapshot.calibration_valid || !hardware.snapshot.origin_valid ||
        hardware.snapshot.current_protection_state != "CONFIGURED") {
        status_.state = PrecheckState::MaintenanceRequired;
        status_.maintenance_required = true;
        if (!hardware.snapshot.calibration_valid) status_.detail = "calibration_required";
        else if (!hardware.snapshot.origin_valid) status_.detail = "origin_required";
        else status_.detail = "current_protection_required";
        if (error) *error = status_.detail;
        (void)hardware_.stopAndDisable(1000);
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
    status_.search_completed = false;
    status_.maintenance_required = false;
    status_.released = false;
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
    last_command_result_ = {};
    sequence_owner_id_.clear();
    command_history_.clear();
    next_restore_attempt_ms_ = 0;
    primary_waypoint_index_ = 0;
    recovery_waypoint_index_ = 0;
    visited_waypoints_ = 0;
    recovery_started_ = false;
    active_waypoint_.reset();
    frozen_subject_box_.reset();
    candidate_subject_box_.reset();
    cooldown_subject_box_.reset();
    cooldown_until_ms_ = 0;
    centering_deadline_ms_ = 0;
    framing_started_ms_ = 0;
    last_visual_frame_id_ = 0;
    missing_subject_frames_ = 0;
    centered_frames_ = 0;
    framing_scores_.clear();
    manual_target_yaw_deg_.reset();
    manual_target_pitch_deg_.reset();
    yaw_boundary_gate_ = {};
    pitch_boundary_gate_ = {};
    session_composition_target_.reset();
    state_before_degrade_ = PrecheckState::Inactive;
    last_phase_ = session::Phase::Precheck;
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
    if (status_.state != PrecheckState::Degraded) {
        state_before_degrade_ = status_.state;
    }
    status_.state = PrecheckState::Degraded;
    status_.manual_lease = false;
    status_.lease_owner_id.clear();
    status_.lease_token.clear();
    status_.lease_expires_at_ms = 0;
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
    status_.state = PrecheckState::ManualAvailable;
    status_.subject_found = subject_found;
    status_.search_completed = true;
    status_.manual_lease = false;
    status_.detail = subject_found ? "subject_found_manual_available"
                                   : "search_timeout_manual_available";
    initializeAuthoritativeTargetLocked();
    if (manual_target_yaw_deg_ && manual_target_pitch_deg_) {
        session_composition_target_ = Waypoint{*manual_target_yaw_deg_,
                                               *manual_target_pitch_deg_};
    }
    frozen_subject_box_.reset();
    framing_scores_.clear();
    if (centering_tracker_) centering_tracker_->reset();
    LOGI("GimbalPrecheckController",
         "PRECHECK search finished subject_found=%d manual_available=1 waypoint=%zu",
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
    const auto elapsed = now_ms - started_ms_;
    const auto remaining = std::max<std::int64_t>(
        1, config_.hard_deadline_ms - elapsed);
    const auto motion_deadline = static_cast<int>(std::min<std::int64_t>(
        offset.deadline_ms, remaining));
    const auto moved = hardware_.moveToVerified(
        waypoint->yaw_deg, waypoint->pitch_deg, motion_deadline, 1.5);
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
    const auto completed_ms = clock_ ? clock_() : now_ms;
    next_waypoint_ms_ = completed_ms + config_.settle_buffer_ms +
                        config_.waypoint_period_ms;
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
        const auto moved = hardware_.moveToVerified(
            waypoint->yaw_deg, waypoint->pitch_deg,
            static_cast<int>(std::min<std::int64_t>(active_waypoint_->deadline_ms,
                                                    4000)),
            1.5);
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
            !visualInCooldownLocked(now_ms, visual) && visual.subject_box) {
            const bool same_subject = candidate_subject_box_ &&
                associateVisualSubject(visual, *candidate_subject_box_).accepted;
            if (subject_count_ == 0 || !same_subject ||
                now_ms - last_subject_evidence_ms_ > config_.visual_fresh_ms) {
                subject_count_ = 1;
                first_subject_ms_ = now_ms;
                candidate_subject_box_ = *visual.subject_box;
            } else {
                ++subject_count_;
                candidate_subject_box_ = *visual.subject_box;
            }
            last_subject_evidence_ms_ = now_ms;
            if (subject_count_ >= config_.subject_observations &&
                now_ms - first_subject_ms_ >= config_.subject_stable_ms) {
                primary_waypoint_index_ = config_.search_waypoints.size();
                recovery_waypoint_index_ = config_.recovery_waypoints.size();
                active_waypoint_.reset();
                (void)beginCandidateLocked(now_ms, visual);
                return;
            }
        } else {
            subject_count_ = 0;
            first_subject_ms_ = 0;
            candidate_subject_box_.reset();
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
    if (status_.released || status_.maintenance_required ||
        now_ms < next_restore_attempt_ms_) return false;
    next_restore_attempt_ms_ = now_ms + 5000;
    CommandResult recovered{true};
    if (hardware.state == State::Fault || hardware.state == State::RecoveryCheck ||
        hardware.recoverable || !hardware.fault_latched_reason.empty()) {
        recovered = hardware_.recover(5000);
    }
    auto aligned = recovered;
    if (aligned.success) aligned = hardware_.enableAndAlign(10000);
    if (!aligned.success) {
        std::string maintenance_detail;
        if (isMaintenanceFailureLocked(aligned, maintenance_detail)) {
            status_.state = PrecheckState::MaintenanceRequired;
            status_.maintenance_required = true;
            status_.detail = maintenance_detail;
        } else {
            degradeLocked(aligned.error.empty() ? "gimbal_recover_failed" : aligned.error);
        }
        return false;
    }
    ++status_.generation;
    next_restore_attempt_ms_ = 0;
    status_.manual_lease = false;
    status_.lease_owner_id.clear();
    status_.lease_token.clear();
    status_.lease_expires_at_ms = 0;
    status_.detail = "gimbal_recovered";
    if (centering_tracker_) centering_tracker_->reset();
    if (phase == session::Phase::Precheck && !status_.search_completed) {
        status_.state = PrecheckState::Searching;
        status_.detail = "recovered_search_resume";
        next_waypoint_ms_ = now_ms + config_.settle_buffer_ms;
        first_subject_ms_ = 0;
        last_subject_evidence_ms_ = 0;
        subject_count_ = 0;
        active_waypoint_.reset();
        frozen_subject_box_.reset();
        last_visual_frame_id_ = 0;
        framing_scores_.clear();
        return true;
    }
    if (session_composition_target_) {
        const auto repositioned = hardware_.moveToVerified(
            session_composition_target_->yaw_deg,
            session_composition_target_->pitch_deg, 8000, 1.5);
        if (!repositioned.success) {
            status_.detail = "gimbal_recovered_fixed_position_unavailable";
        }
    }
    const auto held = hardware_.hold(1000);
    if (!held.success) {
        degradeLocked(held.error.empty() ? "gimbal_recovered_hold_failed" : held.error);
        return false;
    }
    status_.state = phase == session::Phase::Precheck
                        ? PrecheckState::ManualAvailable
                        : PrecheckState::Holding;
    status_.detail = phase == session::Phase::Precheck
                         ? "recovered_manual_available"
                         : "gimbal_recovered_holding";
    return true;
}

void GimbalPrecheckController::maintain(
    session::Phase phase, std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_phase_ = phase;
    if (status_.state == PrecheckState::Inactive ||
        status_.state == PrecheckState::Released ||
        status_.state == PrecheckState::MaintenanceRequired) return;
    if (status_.manual_lease && now_ms >= status_.lease_expires_at_ms) {
        const auto expired_owner = status_.lease_owner_id;
        const auto held = hardware_.hold(1000);
        if (!held.success) {
            degradeLocked(held.error.empty() ? "manual_lease_expiry_hold_failed"
                                             : held.error);
            return;
        }
        clearLeaseLocked(phase, phase == session::Phase::Precheck
                                    ? "manual_lease_expired_available"
                                    : "manual_lease_expired_tracking_resumed");
        auto history = command_history_.find(expired_owner);
        if (history != command_history_.end()) {
            history->second.result = rejected("manual_lease_expired");
            if (sequence_owner_id_ == expired_owner) {
                last_command_result_ = history->second.result;
            }
        }
    }
    const auto hardware = hardware_.status();
    updateBoundaryGatesLocked(hardware, now_ms);
    if (status_.state == PrecheckState::Degraded) {
        (void)restoreLocked(phase, now_ms);
        return;
    }
    if (hardware.state == State::Fault || hardware.state == State::RecoveryCheck) {
        degradeLocked(hardware.fault_latched_reason.empty()
                          ? std::string("gimbal_runtime_fault:") + stateName(hardware.state)
                          : hardware.fault_latched_reason);
        return;
    }
    if (hardware.state != State::Ready && hardware.state != State::Holding &&
        hardware.state != State::Moving) {
        degradeLocked(std::string("gimbal_runtime_unhealthy:") + stateName(hardware.state));
    }
}

bool GimbalPrecheckController::validateSequenceLocked(
    const std::string& session_id, const std::string& owner_id,
    std::uint64_t sequence, CommandResult& result) {
    if (session_id.empty() || session_id != status_.session_id) {
        result = rejected("session_mismatch");
        return false;
    }
    if (owner_id.empty()) {
        result = rejected("owner_id_required");
        return false;
    }
    sequence_owner_id_ = owner_id;
    const auto history = command_history_.find(owner_id);
    last_command_sequence_ = history == command_history_.end()
                                 ? 0
                                 : history->second.sequence;
    last_command_result_ = history == command_history_.end()
                               ? CommandResult{}
                               : history->second.result;
    if (sequence == 0 || sequence < last_command_sequence_) {
        result = rejected("STALE_SEQUENCE");
        return false;
    }
    if (sequence == last_command_sequence_) {
        result = last_command_result_;
        return false;
    }
    return true;
}

void GimbalPrecheckController::rememberCommandLocked(
    std::uint64_t sequence, const CommandResult& result) {
    last_command_sequence_ = sequence;
    last_command_result_ = result;
    if (!sequence_owner_id_.empty()) {
        command_history_[sequence_owner_id_] = CommandHistory{sequence, result};
    }
}

bool GimbalPrecheckController::validateManualCommandLocked(
    const ManualCommandContext& command, std::int64_t now_ms,
    CommandResult& result) {
    if (command.session_id.empty() || command.session_id != status_.session_id) {
        result = rejected("session_mismatch");
        return false;
    }
    if (command.owner_id.empty()) {
        result = rejected("owner_id_required");
        return false;
    }
    if (!status_.manual_lease || status_.state != PrecheckState::Manual) {
        result = rejected("manual_lease_unavailable");
        return false;
    }
    if (status_.lease_owner_id != command.owner_id ||
        status_.lease_token != command.token) {
        result = rejected("manual_lease_mismatch");
        return false;
    }
    if (now_ms >= status_.lease_expires_at_ms) {
        result = rejected("manual_lease_expired");
        return false;
    }
    return validateSequenceLocked(command.session_id, command.owner_id,
                                  command.sequence, result);
}

void GimbalPrecheckController::initializeAuthoritativeTargetLocked() {
    const auto hardware = hardware_.status();
    if (hardware.snapshot.target_yaw10 && hardware.snapshot.target_pitch10) {
        manual_target_yaw_deg_ = normalizeAngle(
            static_cast<double>(*hardware.snapshot.target_yaw10) / 10.0);
        manual_target_pitch_deg_ = normalizeAngle(
            static_cast<double>(*hardware.snapshot.target_pitch10) / 10.0);
    } else {
        manual_target_yaw_deg_ = hardware.yaw_deg;
        manual_target_pitch_deg_ = hardware.pitch_deg;
    }
    status_.authoritative_target_yaw_deg = manual_target_yaw_deg_;
    status_.authoritative_target_pitch_deg = manual_target_pitch_deg_;
}

void GimbalPrecheckController::updateBoundaryGatesLocked(
    const Status& hardware, std::int64_t now_ms) {
    const auto update = [&](BoundaryGate& gate, const std::optional<double>& pose) {
        if (gate.state == BoundaryGateState::Armed && now_ms >= gate.expires_at_ms) {
            gate = {};
            return;
        }
        if (gate.state != BoundaryGateState::Pending || !pose ||
            !hardware.snapshot.settled) return;
        const double offset = shortestAngleDelta(0.0, *pose);
        const double boundary = static_cast<double>(gate.side) *
                                config_.manual_boundary_deg;
        if (std::abs(shortestAngleDelta(boundary, offset)) <=
            config_.boundary_settle_tolerance_deg) {
            gate.state = BoundaryGateState::Armed;
            gate.expires_at_ms = now_ms + config_.boundary_permission_ttl_ms;
        }
    };
    update(yaw_boundary_gate_, hardware.yaw_deg);
    update(pitch_boundary_gate_, hardware.pitch_deg);
}

std::string GimbalPrecheckController::issueLeaseTokenLocked(
    const std::string& owner_id) const {
    return status_.session_id + ":" + owner_id + ":" +
           std::to_string(status_.lease_epoch);
}

void GimbalPrecheckController::clearLeaseLocked(
    session::Phase phase, std::string detail) {
    status_.manual_lease = false;
    status_.lease_owner_id.clear();
    status_.lease_token.clear();
    status_.lease_expires_at_ms = 0;
    status_.state = phase == session::Phase::Precheck
                        ? PrecheckState::ManualAvailable
                        : PrecheckState::Holding;
    status_.detail = std::move(detail);
    if (centering_tracker_) centering_tracker_->reset();
}

CommandResult GimbalPrecheckController::skipAuto(
    const std::string& session_id, std::uint64_t sequence, std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult sequence_result;
    if (!validateSequenceLocked(session_id, "session", sequence,
                                sequence_result)) {
        return sequence_result;
    }
    if (status_.state != PrecheckState::Searching &&
        status_.state != PrecheckState::Centering &&
        status_.state != PrecheckState::Framing &&
        status_.state != PrecheckState::Holding) {
        return rejected("automatic_search_not_active");
    }
    ++status_.generation;
    auto result = hardware_.hold(1000);
    if (!result.success) {
        (void)hardware_.stopAndDisable(1000);
        degradeLocked(result.error.empty() ? "skip_stop_failed" : result.error);
        rememberCommandLocked(sequence, result);
        return result;
    }
    status_.state = PrecheckState::ManualAvailable;
    status_.search_completed = true;
    status_.manual_lease = false;
    status_.detail = "automatic_search_skipped_manual_available";
    initializeAuthoritativeTargetLocked();
    frozen_subject_box_.reset();
    framing_scores_.clear();
    if (centering_tracker_) centering_tracker_->reset();
    rememberCommandLocked(sequence, result);
    return result;
}

CommandResult GimbalPrecheckController::nudge(
    const std::string& session_id, std::uint64_t sequence,
    NudgeDirection direction, std::int64_t now_ms, bool fine_tune) {
    const auto current = snapshot();
    return nudge(ManualCommandContext{session_id, "legacy", current.lease_token,
                                      sequence},
                 direction, now_ms, fine_tune);
}

CommandResult GimbalPrecheckController::nudge(
    const ManualCommandContext& command, NudgeDirection direction,
    std::int64_t now_ms, bool fine_tune) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(command, now_ms, result)) return result;
    if (!manual_target_yaw_deg_ || !manual_target_pitch_deg_) {
        initializeAuthoritativeTargetLocked();
    }
    if (!manual_target_yaw_deg_ || !manual_target_pitch_deg_) {
        result = rejected("gimbal_target_unknown");
        rememberCommandLocked(command.sequence, result);
        return result;
    }
    const double step = fine_tune ? config_.fine_nudge_step_deg
                                  : config_.normal_nudge_step_deg;
    double yaw = *manual_target_yaw_deg_;
    double pitch = *manual_target_pitch_deg_;
    BoundaryGate* gate = nullptr;
    double* selected = nullptr;
    int direction_sign = 0;
    if (direction == NudgeDirection::Left || direction == NudgeDirection::Right) {
        gate = &yaw_boundary_gate_;
        selected = &yaw;
        direction_sign = direction == NudgeDirection::Right ? 1 : -1;
    } else {
        gate = &pitch_boundary_gate_;
        selected = &pitch;
        direction_sign = direction == NudgeDirection::Up ? 1 : -1;
    }
    const double current_offset = shortestAngleDelta(0.0, *selected);
    const bool outward = direction_sign > 0
        ? current_offset >= config_.manual_boundary_deg
        : current_offset <= -config_.manual_boundary_deg;
    if (gate->state == BoundaryGateState::Pending &&
        gate->side == direction_sign && outward) {
        result = rejected("gimbal_boundary_settle_pending");
        result.fine_tune = fine_tune;
        result.resolved_step_deg = step;
        result.motion_status = "boundary_pending";
        result.boundary_paused = true;
        rememberCommandLocked(command.sequence, result);
        return result;
    }
    bool consume_permission = gate->state == BoundaryGateState::Armed &&
                              gate->side == direction_sign && outward;
    const double requested_offset = current_offset + direction_sign * step;
    bool boundary_stop = false;
    if (!consume_permission && std::abs(current_offset) < config_.manual_boundary_deg &&
        ((direction_sign > 0 && requested_offset > config_.manual_boundary_deg) ||
         (direction_sign < 0 && requested_offset < -config_.manual_boundary_deg))) {
        *selected = normalizeAngle(direction_sign * config_.manual_boundary_deg);
        boundary_stop = true;
    } else {
        *selected = offsetAngle(*selected, direction_sign * step);
    }
    // Manual input is acknowledged at the firmware target-acceptance boundary.
    // Settlement remains observable through the shared gimbal snapshot, so a
    // BLE/network callback never waits several seconds for the motor to arrive.
    result = hardware_.moveToManual(yaw, pitch, 500);
    result.fine_tune = fine_tune;
    result.resolved_step_deg = step;
    result.boundary_stop_planned = boundary_stop;
    result.boundary_paused = boundary_stop;
    result.motion_status = boundary_stop ? "moving_to_boundary" : "firmware_accepted";
    if (result.success) {
        manual_target_yaw_deg_ = yaw;
        manual_target_pitch_deg_ = pitch;
        status_.authoritative_target_yaw_deg = yaw;
        status_.authoritative_target_pitch_deg = pitch;
        if (boundary_stop) {
            gate->state = BoundaryGateState::Pending;
            gate->side = direction_sign;
            gate->expires_at_ms = 0;
        } else if (consume_permission || !outward) {
            *gate = {};
        }
    } else if (result.error != "target_out_of_bounds" &&
               result.error_code != "TARGET_OUT_OF_RANGE") {
        degradeLocked(result.error.empty() ? "gimbal_nudge_failed" : result.error);
    }
    rememberCommandLocked(command.sequence, result);
    return result;
}

CommandResult GimbalPrecheckController::center(
    const std::string& session_id, std::uint64_t sequence, std::int64_t now_ms) {
    const auto current = snapshot();
    return center(ManualCommandContext{session_id, "legacy", current.lease_token,
                                       sequence},
                  now_ms);
}

CommandResult GimbalPrecheckController::center(
    const ManualCommandContext& command, std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(command, now_ms, result)) return result;
    if (!config_.center) return rejected("gimbal_center_unverified");
    const auto center = resolveWaypointLocked(*config_.center);
    if (!center) return rejected("gimbal_alignment_reference_unknown");
    result = hardware_.moveTo(center->yaw_deg, center->pitch_deg, 500);
    if (result.success) {
        manual_target_yaw_deg_ = center->yaw_deg;
        manual_target_pitch_deg_ = center->pitch_deg;
        status_.authoritative_target_yaw_deg = center->yaw_deg;
        status_.authoritative_target_pitch_deg = center->pitch_deg;
    } else {
        degradeLocked(result.error.empty() ? "gimbal_center_failed" : result.error);
    }
    rememberCommandLocked(command.sequence, result);
    return result;
}

CommandResult GimbalPrecheckController::stopMotion(
    const std::string& session_id, std::uint64_t sequence, std::int64_t now_ms) {
    const auto current = snapshot();
    return stopMotion(ManualCommandContext{session_id, "legacy",
                                           current.lease_token, sequence},
                      now_ms);
}

CommandResult GimbalPrecheckController::stopMotion(
    const ManualCommandContext& command, std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(command, now_ms, result)) return result;
    result = hardware_.hold(1000);
    if (!result.success) degradeLocked(result.error.empty() ? "gimbal_stop_failed" : result.error);
    else initializeAuthoritativeTargetLocked();
    rememberCommandLocked(command.sequence, result);
    return result;
}

CommandResult GimbalPrecheckController::confirmPosition(
    const std::string& session_id, std::uint64_t sequence, std::int64_t now_ms) {
    const auto current = snapshot();
    return confirmPosition(ManualCommandContext{session_id, "legacy",
                                                current.lease_token, sequence},
                           session::Phase::Precheck, now_ms);
}

CommandResult GimbalPrecheckController::confirmPosition(
    const ManualCommandContext& command, session::Phase phase,
    std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(command, now_ms, result)) return result;
    result = hardware_.hold(1000);
    if (!result.success) {
        degradeLocked(result.error.empty() ? "gimbal_hold_failed" : result.error);
        return result;
    }
    const auto measured = hardware_.status();
    if (!measured.yaw_deg || !measured.pitch_deg) {
        result = rejected("gimbal_composition_angle_unknown");
        rememberCommandLocked(command.sequence, result);
        return result;
    }
    session_composition_target_ = Waypoint{
        normalizeAngle(*measured.yaw_deg), normalizeAngle(*measured.pitch_deg)};
    ++status_.generation;
    frozen_subject_box_.reset();
    cooldown_subject_box_.reset();
    framing_scores_.clear();
    active_waypoint_.reset();
    if (centering_tracker_) centering_tracker_->reset();
    clearLeaseLocked(phase, "position_confirmed");
    rememberCommandLocked(command.sequence, result);
    return result;
}

CommandResult GimbalPrecheckController::realign(
    const std::string& session_id, std::uint64_t sequence, std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult sequence_result;
    if (!validateSequenceLocked(session_id, "session", sequence,
                                sequence_result)) {
        return sequence_result;
    }
    const auto hardware = hardware_.status();
    if (hardware.state != State::Disabled || !hardware.recoverable) {
        return rejected("gimbal_not_recoverable");
    }
    auto result = hardware_.recover(5000);
    if (result.success) result = hardware_.enableAndAlign(10000);
    if (!result.success) {
        degradeLocked(result.error.empty() ? "gimbal_realign_failed" : result.error);
        rememberCommandLocked(sequence, result);
        return result;
    }
    status_.state = status_.search_completed ? PrecheckState::ManualAvailable
                                             : PrecheckState::Searching;
    status_.manual_lease = false;
    status_.detail = status_.search_completed
                         ? "recovered_manual_available"
                         : "recovered_search_resume";
    frozen_subject_box_.reset();
    framing_scores_.clear();
    if (centering_tracker_) centering_tracker_->reset();
    rememberCommandLocked(sequence, result);
    return result;
}

CommandResult GimbalPrecheckController::acquireSessionManualControl(
    const std::string& session_id, std::uint64_t sequence,
    session::Phase phase, std::int64_t now_ms) {
    return manualTakeover(session_id, "legacy", sequence, phase, now_ms);
}

CommandResult GimbalPrecheckController::manualTakeover(
    const std::string& session_id, const std::string& owner_id,
    std::uint64_t sequence, session::Phase phase, std::int64_t now_ms,
    PrecheckStatus* issued) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.manual_lease && now_ms >= status_.lease_expires_at_ms) {
        const auto expired_owner = status_.lease_owner_id;
        const auto held = hardware_.hold(1000);
        if (!held.success) {
            degradeLocked(held.error.empty() ? "manual_lease_expiry_hold_failed"
                                             : held.error);
            if (issued) *issued = status_;
            return held;
        }
        clearLeaseLocked(phase, phase == session::Phase::Precheck
                                    ? "manual_lease_expired_available"
                                    : "manual_lease_expired_tracking_resumed");
        // A duplicate takeover from the expired lease must never replay the
        // cached success containing its now-invalid token.
        auto history = command_history_.find(expired_owner);
        if (history != command_history_.end()) {
            history->second.result = rejected("manual_lease_expired");
            if (sequence_owner_id_ == expired_owner) {
                last_command_result_ = history->second.result;
            }
        }
    }
    CommandResult sequence_result;
    if (!validateSequenceLocked(session_id, owner_id, sequence,
                                sequence_result)) {
        if (issued) *issued = status_;
        return sequence_result;
    }
    if (phase != session::Phase::Precheck && phase != session::Phase::Focusing) {
        return rejected("manual_control_phase_forbidden");
    }
    if (status_.state == PrecheckState::Released ||
        status_.state == PrecheckState::MaintenanceRequired ||
        status_.state == PrecheckState::Degraded ||
        status_.state == PrecheckState::Inactive) {
        return rejected("manual_control_unavailable");
    }
    if (status_.manual_lease && now_ms < status_.lease_expires_at_ms &&
        status_.lease_owner_id != owner_id) {
        return rejected("manual_lease_owned_by_other_client");
    }
    auto result = hardware_.hold(1000);
    if (!result.success) {
        degradeLocked(result.error.empty() ? "session_manual_hold_failed" : result.error);
        rememberCommandLocked(sequence, result);
        return result;
    }
    status_.search_completed = true;
    status_.state = PrecheckState::Manual;
    status_.manual_lease = true;
    status_.lease_owner_id = owner_id;
    ++status_.lease_epoch;
    status_.lease_token = issueLeaseTokenLocked(owner_id);
    status_.lease_expires_at_ms = now_ms + config_.manual_lease_ttl_ms;
    status_.detail = "manual_control";
    initializeAuthoritativeTargetLocked();
    if (centering_tracker_) centering_tracker_->reset();
    rememberCommandLocked(sequence, result);
    if (issued) *issued = status_;
    return result;
}

CommandResult GimbalPrecheckController::keepManualLease(
    const ManualCommandContext& command, session::Phase phase,
    std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (phase != session::Phase::Precheck && phase != session::Phase::Focusing) {
        return rejected("manual_control_phase_forbidden");
    }
    if (!validateManualCommandLocked(command, now_ms, result)) return result;
    result.success = true;
    status_.lease_expires_at_ms = now_ms + config_.manual_lease_ttl_ms;
    rememberCommandLocked(command.sequence, result);
    return result;
}

CommandResult GimbalPrecheckController::releaseSessionManualControl(
    const std::string& session_id, std::uint64_t sequence,
    session::Phase phase, std::int64_t now_ms) {
    const auto current = snapshot();
    return releaseManualControl(
        ManualCommandContext{session_id, "legacy", current.lease_token, sequence},
        phase, now_ms);
}

CommandResult GimbalPrecheckController::releaseManualControl(
    const ManualCommandContext& command, session::Phase phase,
    std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(command, now_ms, result)) return result;
    result = hardware_.hold(1000);
    if (!result.success) {
        degradeLocked(result.error.empty() ? "session_manual_release_failed" : result.error);
        return result;
    }
    clearLeaseLocked(phase, phase == session::Phase::Precheck
                                ? "manual_available"
                                : "session_tracking_resumed");
    rememberCommandLocked(command.sequence, result);
    return result;
}

CommandResult GimbalPrecheckController::forceRelease(
    const ManualCommandContext& command, session::Phase /*phase*/,
    std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult result;
    if (!validateManualCommandLocked(command, now_ms, result)) return result;
    result = hardware_.stopAndDisable(1000);
    status_.manual_lease = false;
    status_.lease_owner_id.clear();
    status_.lease_token.clear();
    status_.lease_expires_at_ms = 0;
    status_.search_completed = true;
    status_.released = true;
    status_.state = PrecheckState::Released;
    status_.detail = result.success ? "released" : "released_stop_failed";
    rememberCommandLocked(command.sequence, result);
    return result;
}

void GimbalPrecheckController::holdForSession(std::int64_t /*now_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == PrecheckState::Inactive ||
        status_.state == PrecheckState::Degraded ||
        status_.state == PrecheckState::MaintenanceRequired ||
        status_.state == PrecheckState::Released) return;
    const auto result = hardware_.hold(1000);
    if (!result.success) {
        degradeLocked(result.error.empty() ? "gimbal_hold_failed" : result.error);
        return;
    }
    status_.state = PrecheckState::Holding;
    status_.manual_lease = false;
    status_.lease_owner_id.clear();
    status_.lease_token.clear();
    status_.lease_expires_at_ms = 0;
    if (centering_tracker_) centering_tracker_->reset();
}

void GimbalPrecheckController::stop() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == PrecheckState::Inactive) return;
    const auto next_generation = status_.generation + 1;
    const auto hardware = hardware_.status();
    bool center_attempted = false;
    bool center_succeeded = false;
    if (status_.state != PrecheckState::Released &&
        (hardware.state == State::Ready || hardware.state == State::Holding ||
         hardware.state == State::Moving) && !hardware.fault_active) {
        center_attempted = true;
        center_succeeded = hardware_.moveToVerified(
            0.0, 0.0, 8000, 1.5).success;
        if (!center_succeeded) {
            LOGW("GimbalPrecheckController", "RETURN_TO_ORIGIN_FAILED");
        }
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

CommandResult GimbalPrecheckController::executeMaintenance(
    const MaintenanceRequest& request, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != PrecheckState::Inactive &&
        status_.state != PrecheckState::MaintenanceRequired &&
        status_.state != PrecheckState::Degraded) {
        return rejected("maintenance_requires_inactive_gimbal");
    }
    return hardware_.executeMaintenance(request, timeout_ms);
}

PrecheckStatus GimbalPrecheckController::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto snapshot = status_;
    snapshot.last_command_sequence = last_command_sequence_;
    return snapshot;
}

Status GimbalPrecheckController::snapshotHardware() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hardware_.status();
}

}  // namespace edgefocus::gimbal
