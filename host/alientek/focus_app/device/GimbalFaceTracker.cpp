#include "GimbalFaceTracker.h"

#include <algorithm>
#include <cmath>

namespace edgefocus {

GimbalFaceTracker::GimbalFaceTracker(GimbalTrackingConfig config)
    : config_(config) {}

void GimbalFaceTracker::reset() {
    has_smoothed_target_ = false;
    smoothed_x_ = 0.5f;
    smoothed_y_ = 0.5f;
    has_target_angles_ = false;
    target_angle_x_ = 0.0f;
    target_angle_y_ = 0.0f;
    last_target_seen_ms_ = 0;
    last_command_ms_ = 0;
}

float GimbalFaceTracker::clamp01(float value) {
    if (!std::isfinite(value)) return 0.5f;
    return std::clamp(value, 0.0f, 1.0f);
}

float GimbalFaceTracker::outsideDeadzone(float error, float deadzone) {
    const float magnitude = std::fabs(error);
    if (magnitude <= deadzone) return 0.0f;
    const float denominator = std::max(0.001f, 1.0f - deadzone);
    const float normalized = std::min((magnitude - deadzone) / denominator, 1.0f);
    return std::copysign(normalized, error);
}

int GimbalFaceTracker::clampAngleX(float value) {
    if (!std::isfinite(value)) return 0;
    return static_cast<int>(std::lround(std::clamp(value, 0.0f, 180.0f)));
}

int GimbalFaceTracker::wrapAngleY(float value) {
    if (!std::isfinite(value)) return 0;
    float wrapped = std::fmod(value, 360.0f);
    if (wrapped < 0.0f) wrapped += 360.0f;
    return static_cast<int>(std::lround(wrapped)) % 360;
}

std::optional<GimbalTrackingCommand> GimbalFaceTracker::update(const GimbalTrackingInput& input) {
    if (!input.enabled || !input.gimbal_ready) {
        reset();
        return std::nullopt;
    }

    const int64_t now_ms = input.now_ms > 0 ? input.now_ms : input.target_timestamp_ms;
    if (!input.target_valid || input.target_confidence < 0.25f) {
        if (last_target_seen_ms_ > 0 &&
            now_ms - last_target_seen_ms_ > config_.target_lost_timeout_ms) {
            reset();
        }
        return std::nullopt;
    }
    if (input.target_timestamp_ms > 0 &&
        now_ms - input.target_timestamp_ms > config_.target_lost_timeout_ms) {
        reset();
        return std::nullopt;
    }

    const float raw_x = clamp01(input.target_center_x);
    const float raw_y = clamp01(input.target_center_y);
    const float alpha = std::clamp(config_.smoothing, 0.01f, 1.0f);
    if (!has_smoothed_target_) {
        smoothed_x_ = raw_x;
        smoothed_y_ = raw_y;
        has_smoothed_target_ = true;
    } else {
        smoothed_x_ += alpha * (raw_x - smoothed_x_);
        smoothed_y_ += alpha * (raw_y - smoothed_y_);
    }
    last_target_seen_ms_ = now_ms;

    if (!has_target_angles_) {
        target_angle_x_ = input.current_angle_x;
        target_angle_y_ = input.current_angle_y;
        has_target_angles_ = true;
    }

    const int min_period_ms = std::max(1, 1000 / std::max(1, config_.command_hz));
    if (last_command_ms_ > 0 && now_ms - last_command_ms_ < min_period_ms) {
        return std::nullopt;
    }

    const float ex = outsideDeadzone((smoothed_x_ - 0.5f) / 0.5f, config_.deadzone_x);
    const float ey = outsideDeadzone((smoothed_y_ - 0.5f) / 0.5f, config_.deadzone_y);
    if (ex == 0.0f && ey == 0.0f) {
        last_command_ms_ = now_ms;
        return std::nullopt;
    }

    const float dt_s = last_command_ms_ > 0
                           ? std::min((now_ms - last_command_ms_) / 1000.0f, 0.2f)
                           : std::min(min_period_ms / 1000.0f, 0.2f);
    const float yaw_delta = ex * config_.gain_x * config_.max_speed_x_deg_s *
                            static_cast<float>(config_.x_direction) * dt_s;
    const float pitch_delta = ey * config_.gain_y * config_.max_speed_y_deg_s *
                              static_cast<float>(config_.y_direction) * dt_s;

    // Existing firmware command order is SET XY <x_angle> <y_angle>. The
    // Python prototype maps horizontal image error to Y axis and vertical
    // image error to X axis; keep that mapping here.
    target_angle_y_ += yaw_delta;
    target_angle_x_ += pitch_delta;
    last_command_ms_ = now_ms;

    GimbalTrackingCommand command;
    command.x_deg = clampAngleX(target_angle_x_);
    command.y_deg = wrapAngleY(target_angle_y_);
    command.reason = "face_tracking";
    return command;
}

}  // namespace edgefocus
