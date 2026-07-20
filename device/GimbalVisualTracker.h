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
    double yaw_gain_deg{8.0};
    double pitch_gain_deg{7.0};
    double maximum_step_deg{2.0};
    double smoothing_alpha{0.35};
    double yaw_direction{1.0};
    double pitch_direction{-1.0};
    std::int64_t visual_fresh_ms{600};
    std::int64_t minimum_command_period_ms{100};
    std::int64_t lost_hold_ms{1500};
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
};

}  // namespace edgefocus::gimbal
