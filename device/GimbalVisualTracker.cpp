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
           deadband_y < 0.5 && yaw_gain_deg > 0.0 && pitch_gain_deg > 0.0 &&
           maximum_step_deg > 0.0 && smoothing_alpha > 0.0 && smoothing_alpha <= 1.0 &&
           std::abs(yaw_direction) == 1.0 && std::abs(pitch_direction) == 1.0 &&
           visual_fresh_ms > 0 && minimum_command_period_ms >= 100 && lost_hold_ms > 0 &&
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
    if (visual.frame_id == 0 || visual.frame_id <= status_.last_frame_id) {
        return ignored("duplicate_or_missing_frame");
    }
    status_.last_frame_id = visual.frame_id;
    const bool fresh = visual.evidence_timestamp_ms > 0 &&
                       now_ms >= visual.evidence_timestamp_ms &&
                       now_ms - visual.evidence_timestamp_ms <= config_.visual_fresh_ms;
    const bool present = visual.presence && *visual.presence >= 0.5;
    const auto point = fresh && present
        ? visualTargetPoint(visual, config_.keypoint_confidence)
        : std::nullopt;
    if (!point) {
        if (status_.last_target_ms == 0 || now_ms - status_.last_target_ms >= config_.lost_hold_ms) {
            status_.tracking = false;
            status_.detail = "target_lost_holding";
            smoothing_ready_ = false;
        }
        return ignored("target_unavailable");
    }
    status_.last_target_ms = now_ms;
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

    double error_x = point->first - config_.target_x;
    double error_y = point->second - config_.target_y;
    if (std::abs(error_x) <= config_.deadband_x) error_x = 0.0;
    if (std::abs(error_y) <= config_.deadband_y) error_y = 0.0;
    if (!smoothing_ready_) {
        smoothed_x_ = error_x;
        smoothed_y_ = error_y;
        smoothing_ready_ = true;
    } else {
        smoothed_x_ += config_.smoothing_alpha * (error_x - smoothed_x_);
        smoothed_y_ += config_.smoothing_alpha * (error_y - smoothed_y_);
    }
    if (error_x == 0.0 && error_y == 0.0 &&
        std::abs(smoothed_x_) < config_.deadband_x * 0.25 &&
        std::abs(smoothed_y_) < config_.deadband_y * 0.25) {
        status_.tracking = true;
        status_.detail = "target_in_deadband";
        return ignored("target_in_deadband");
    }

    const double yaw_step = std::clamp(
        smoothed_x_ * config_.yaw_gain_deg * config_.yaw_direction,
        -config_.maximum_step_deg, config_.maximum_step_deg);
    const double pitch_step = std::clamp(
        smoothed_y_ * config_.pitch_gain_deg * config_.pitch_direction,
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
