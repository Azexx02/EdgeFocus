#pragma once

#include "platform/gimbal/IGimbalSessionHardware.h"
#include "session/SessionDomain.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace edgefocus::gimbal {

struct TrackingConfig {
    double target_x{0.5};
    double target_y{1.0 / 3.0};
    double deadband_x{0.06};
    double deadband_y{0.07};
    double reentry_deadband_x{0.09};
    double reentry_deadband_y{0.10};
    double yaw_gain_deg{8.0};
    double pitch_gain_deg{7.0};
    double maximum_step_deg{2.0};
    double smoothing_alpha{0.35};
    double maximum_yaw_velocity_deg_s{12.0};
    double maximum_pitch_velocity_deg_s{10.0};
    double maximum_acceleration_deg_s2{24.0};
    double minimum_target_delta_deg{0.35};
    double yaw_direction{1.0};
    double pitch_direction{-1.0};
    std::int64_t visual_fresh_ms{600};
    std::int64_t minimum_command_period_ms{100};
    std::int64_t lost_hold_ms{1500};
    std::int64_t intent_hold_ms{450};
    std::int64_t intent_decay_ms{700};
    unsigned reversal_confirmation_frames{2};
    double keypoint_confidence{0.35};

    bool verified() const noexcept;
};

struct TrackingStatus {
    bool tracking{};
    std::uint64_t last_frame_id{};
    std::int64_t last_target_ms{};
    std::int64_t last_command_ms{};
    std::string detail{"idle"};
};

class GimbalVisualTracker {
public:
    GimbalVisualTracker(IGimbalSessionHardware& hardware, TrackingConfig config);

    void reset() noexcept;
    CommandResult update(std::int64_t now_ms, const session::VisualSnapshot& visual);
    TrackingStatus status() const;

private:
    static CommandResult ignored(std::string detail);

    IGimbalSessionHardware& hardware_;
    TrackingConfig config_;
    mutable std::mutex mutex_;
    TrackingStatus status_;
    std::optional<double> target_yaw_deg_;
    std::optional<double> target_pitch_deg_;
    double smoothed_x_{};
    double smoothed_y_{};
    bool smoothing_ready_{};
    bool yaw_engaged_{};
    bool pitch_engaged_{};
    double intent_x_{};
    double intent_y_{};
    double yaw_velocity_deg_s_{};
    double pitch_velocity_deg_s_{};
    double pending_yaw_delta_deg_{};
    double pending_pitch_delta_deg_{};
    int pending_yaw_reversal_{};
    int pending_pitch_reversal_{};
    unsigned pending_yaw_reversal_frames_{};
    unsigned pending_pitch_reversal_frames_{};
    std::int64_t last_plan_ms_{};
};

}  // namespace edgefocus::gimbal
