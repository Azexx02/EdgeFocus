#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace edgefocus {

struct GimbalTrackingConfig {
    int command_hz = 8;
    float deadzone_x = 0.05f;
    float deadzone_y = 0.10f;
    float max_speed_x_deg_s = 35.0f;
    float max_speed_y_deg_s = 10.0f;
    float gain_x = 1.3f;
    float gain_y = 0.72f;
    int x_direction = 1;
    int y_direction = 1;
    float smoothing = 0.15f;
    int64_t target_lost_timeout_ms = 800;
};

struct GimbalTrackingInput {
    bool enabled = false;
    bool gimbal_ready = false;
    bool target_valid = false;
    int64_t target_timestamp_ms = 0;
    float target_center_x = 0.5f;
    float target_center_y = 0.5f;
    float target_confidence = 0.0f;
    float current_angle_x = 0.0f;
    float current_angle_y = 0.0f;
    int64_t now_ms = 0;
};

struct GimbalTrackingCommand {
    int x_deg = 0;
    int y_deg = 0;
    std::string reason;
};

class GimbalFaceTracker {
public:
    explicit GimbalFaceTracker(GimbalTrackingConfig config = {});

    std::optional<GimbalTrackingCommand> update(const GimbalTrackingInput& input);
    void reset();

private:
    static float outsideDeadzone(float error, float deadzone);
    static float clamp01(float value);
    static int clampAngleX(float value);
    static int wrapAngleY(float value);

    GimbalTrackingConfig config_;
    bool has_smoothed_target_ = false;
    float smoothed_x_ = 0.5f;
    float smoothed_y_ = 0.5f;
    bool has_target_angles_ = false;
    float target_angle_x_ = 0.0f;
    float target_angle_y_ = 0.0f;
    int64_t last_target_seen_ms_ = 0;
    int64_t last_command_ms_ = 0;
};

}  // namespace edgefocus
