#include "device/GimbalVisualTracker.h"

#include "device/GimbalVisualEvidence.h"
#include "platform/gimbal/GimbalProtocol.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace edgefocus::gimbal {

bool TrackingConfig::verified() const noexcept {
    return target_x >= 0.0 && target_x <= 1.0 && target_y >= 0.0 && target_y <= 1.0 &&
           deadband_x >= 0.0 && deadband_x < 0.5 && deadband_y >= 0.0 &&
           deadband_y < 0.5 && reentry_deadband_x > deadband_x &&
           reentry_deadband_x < 0.5 && reentry_deadband_y > deadband_y &&
           reentry_deadband_y < 0.5 && yaw_gain_deg > 0.0 && pitch_gain_deg > 0.0 &&
           maximum_step_deg > 0.0 && smoothing_alpha > 0.0 && smoothing_alpha <= 1.0 &&
           maximum_yaw_velocity_deg_s > 0.0 && maximum_pitch_velocity_deg_s > 0.0 &&
           maximum_acceleration_deg_s2 > 0.0 && minimum_target_delta_deg > 0.0 &&
           std::abs(yaw_direction) == 1.0 && std::abs(pitch_direction) == 1.0 &&
           visual_fresh_ms > 0 && minimum_command_period_ms >= 50 && lost_hold_ms > 0 &&
           intent_hold_ms > 0 && intent_decay_ms > intent_hold_ms &&
           intent_decay_ms <= lost_hold_ms && reversal_confirmation_frames > 0 &&
           keypoint_confidence >= 0.0 && keypoint_confidence <= 1.0;
}

GimbalVisualTracker::GimbalVisualTracker(
    IGimbalSessionHardware& hardware, TrackingConfig config)
    : hardware_(hardware), config_(std::move(config)) {}

void GimbalVisualTracker::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = {};
    target_yaw_deg_.reset();
    target_pitch_deg_.reset();
    smoothed_x_ = 0.0;
    smoothed_y_ = 0.0;
    smoothing_ready_ = false;
    yaw_engaged_ = false;
    pitch_engaged_ = false;
    intent_x_ = intent_y_ = 0.0;
    yaw_velocity_deg_s_ = pitch_velocity_deg_s_ = 0.0;
    pending_yaw_delta_deg_ = pending_pitch_delta_deg_ = 0.0;
    pending_yaw_reversal_ = pending_pitch_reversal_ = 0;
    pending_yaw_reversal_frames_ = pending_pitch_reversal_frames_ = 0;
    last_plan_ms_ = 0;
}

CommandResult GimbalVisualTracker::ignored(std::string detail) {
    CommandResult result;
    result.success = true;
    result.error = std::move(detail);
    return result;
}

CommandResult GimbalVisualTracker::update(
    std::int64_t now_ms, const session::VisualSnapshot& visual) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.verified()) {
        return {false, false, "tracking_config_unverified", {}, {}};
    }
    const bool new_frame = visual.frame_id > status_.last_frame_id;
    if (new_frame) {
        status_.last_frame_id = visual.frame_id;
        const bool fresh = visual.evidence_timestamp_ms > 0 &&
                           now_ms >= visual.evidence_timestamp_ms &&
                           now_ms - visual.evidence_timestamp_ms <= config_.visual_fresh_ms;
        const bool present = visual.presence && *visual.presence >= 0.5;
        const auto point = fresh && present
            ? visualTargetPoint(visual, config_.keypoint_confidence)
            : std::nullopt;
        if (point) {
            status_.last_target_ms = now_ms;
            const double raw_x = point->first - config_.target_x;
            const double raw_y = point->second - config_.target_y;
            if (!smoothing_ready_) {
                smoothed_x_ = raw_x;
                smoothed_y_ = raw_y;
                smoothing_ready_ = true;
            } else {
                smoothed_x_ += config_.smoothing_alpha * (raw_x - smoothed_x_);
                smoothed_y_ += config_.smoothing_alpha * (raw_y - smoothed_y_);
            }
            auto update_axis = [&](double value, double inner, double outer,
                                   bool& engaged, double& intent, double velocity,
                                   int& pending_sign, unsigned& pending_frames) {
                if (engaged && std::abs(value) <= inner) engaged = false;
                else if (!engaged && std::abs(value) >= outer) engaged = true;
                const double candidate = engaged ? value : 0.0;
                const int sign = candidate > 0.0 ? 1 : candidate < 0.0 ? -1 : 0;
                const int velocity_sign = velocity > 0.05 ? 1 : velocity < -0.05 ? -1 : 0;
                if (sign != 0 && velocity_sign != 0 && sign != velocity_sign) {
                    if (pending_sign == sign) ++pending_frames;
                    else { pending_sign = sign; pending_frames = 1; }
                    if (pending_frames < config_.reversal_confirmation_frames) {
                        intent = 0.0;
                        return;
                    }
                } else {
                    pending_sign = 0;
                    pending_frames = 0;
                }
                intent = candidate;
            };
            update_axis(smoothed_x_, config_.deadband_x, config_.reentry_deadband_x,
                        yaw_engaged_, intent_x_, yaw_velocity_deg_s_,
                        pending_yaw_reversal_, pending_yaw_reversal_frames_);
            update_axis(smoothed_y_, config_.deadband_y, config_.reentry_deadband_y,
                        pitch_engaged_, intent_y_, pitch_velocity_deg_s_,
                        pending_pitch_reversal_, pending_pitch_reversal_frames_);
        }
    }
    const auto target_age_ms = status_.last_target_ms > 0
        ? now_ms - status_.last_target_ms : config_.lost_hold_ms;
    if (target_age_ms >= config_.lost_hold_ms) {
        status_.tracking = false;
        status_.detail = "target_lost_holding";
        smoothing_ready_ = false;
        intent_x_ = intent_y_ = 0.0;
        yaw_velocity_deg_s_ = pitch_velocity_deg_s_ = 0.0;
        pending_yaw_delta_deg_ = pending_pitch_delta_deg_ = 0.0;
        return ignored("target_lost_holding");
    }
    if (status_.last_command_ms != 0 &&
        now_ms - status_.last_command_ms < config_.minimum_command_period_ms) {
        return ignored("tracking_rate_limited");
    }

    const auto hardware = hardware_.status();
    if (hardware.state != State::Holding && hardware.state != State::Moving) {
        status_.tracking = false;
        status_.detail = "hardware_not_ready";
        return {false, false, "gimbal_tracking_hardware_not_ready", {}, {}};
    }
    if (!target_yaw_deg_ || !target_pitch_deg_) {
        if (!hardware.yaw_deg || !hardware.pitch_deg) {
            return {false, false, "gimbal_tracking_angle_unknown", {}, {}};
        }
        target_yaw_deg_ = *hardware.yaw_deg;
        target_pitch_deg_ = *hardware.pitch_deg;
    }

    double freshness_scale = 1.0;
    if (target_age_ms > config_.intent_hold_ms) {
        freshness_scale = std::clamp(
            1.0 - static_cast<double>(target_age_ms - config_.intent_hold_ms) /
                      static_cast<double>(config_.intent_decay_ms - config_.intent_hold_ms),
            0.0, 1.0);
    }
    const double desired_yaw_velocity = std::clamp(
        intent_x_ * config_.yaw_gain_deg * 4.0 * config_.yaw_direction * freshness_scale,
        -config_.maximum_yaw_velocity_deg_s, config_.maximum_yaw_velocity_deg_s);
    const double desired_pitch_velocity = std::clamp(
        intent_y_ * config_.pitch_gain_deg * 4.0 * config_.pitch_direction * freshness_scale,
        -config_.maximum_pitch_velocity_deg_s, config_.maximum_pitch_velocity_deg_s);
    const double dt = last_plan_ms_ == 0 ? 0.1 :
        std::clamp(static_cast<double>(now_ms - last_plan_ms_) / 1000.0, 0.05, 0.2);
    last_plan_ms_ = now_ms;
    const double max_velocity_change = config_.maximum_acceleration_deg_s2 * dt;
    yaw_velocity_deg_s_ += std::clamp(desired_yaw_velocity - yaw_velocity_deg_s_,
                                      -max_velocity_change, max_velocity_change);
    pitch_velocity_deg_s_ += std::clamp(desired_pitch_velocity - pitch_velocity_deg_s_,
                                        -max_velocity_change, max_velocity_change);
    if (std::abs(yaw_velocity_deg_s_) < 0.02 && std::abs(desired_yaw_velocity) < 0.02)
        yaw_velocity_deg_s_ = 0.0;
    if (std::abs(pitch_velocity_deg_s_) < 0.02 && std::abs(desired_pitch_velocity) < 0.02)
        pitch_velocity_deg_s_ = 0.0;
    pending_yaw_delta_deg_ += yaw_velocity_deg_s_ * dt;
    pending_pitch_delta_deg_ += pitch_velocity_deg_s_ * dt;
    if (std::abs(pending_yaw_delta_deg_) < config_.minimum_target_delta_deg &&
        std::abs(pending_pitch_delta_deg_) < config_.minimum_target_delta_deg) {
        status_.tracking = true;
        status_.detail = (yaw_velocity_deg_s_ == 0.0 && pitch_velocity_deg_s_ == 0.0)
            ? "target_in_deadband" : "tracking_planning";
        return ignored(status_.detail);
    }
    const double yaw_step = std::clamp(pending_yaw_delta_deg_,
        -config_.maximum_step_deg, config_.maximum_step_deg);
    const double pitch_step = std::clamp(pending_pitch_delta_deg_,
        -config_.maximum_step_deg, config_.maximum_step_deg);
    const double next_yaw = offsetAngle(*target_yaw_deg_, yaw_step);
    const double next_pitch = offsetAngle(*target_pitch_deg_, pitch_step);
    auto result = hardware_.trackTo(next_yaw, next_pitch, 500);
    if (!result.success) {
        status_.tracking = false;
        status_.detail = result.error.empty() ? "track_command_failed" : result.error;
        return result;
    }
    target_yaw_deg_ = next_yaw;
    target_pitch_deg_ = next_pitch;
    pending_yaw_delta_deg_ -= yaw_step;
    pending_pitch_delta_deg_ -= pitch_step;
    status_.last_command_ms = now_ms;
    status_.tracking = true;
    status_.detail = "tracking";
    return result;
}

TrackingStatus GimbalVisualTracker::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

}  // namespace edgefocus::gimbal
