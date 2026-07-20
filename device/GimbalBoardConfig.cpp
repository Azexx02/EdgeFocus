#include "device/GimbalBoardConfig.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

namespace edgefocus::gimbal {
namespace {

std::optional<double> numberOrNone(const nlohmann::json& value, const char* key) {
    if (!value.contains(key) || value[key].is_null() || !value[key].is_number()) return std::nullopt;
    return value[key].get<double>();
}

std::int64_t continuousDeadlineMs(double yaw_distance,
                                  double pitch_distance,
                                  double speed_deg_s) {
    return static_cast<std::int64_t>(std::max(
        1200.0,
        std::max(yaw_distance, pitch_distance) / speed_deg_s * 1000.0 +
            700.0));
}

}  // namespace

GimbalBoardConfig loadGimbalBoardConfig(const std::string& path) {
    GimbalBoardConfig result;
    std::ifstream stream(path);
    if (!stream) {
        result.error = "cannot_open_gimbal_board_config";
        return result;
    }
    try {
        nlohmann::json config;
        stream >> config;
        if (config.value("schema_version", 0) != 3) {
            result.error = "unsupported_gimbal_board_schema";
            return result;
        }
        const std::string control_mode = config.value("control_mode", "uart_only");
        if (control_mode == "uart_only") {
            result.control_mode = ControlMode::UartOnly;
        } else if (control_mode == "uart_gpio_interlock") {
            result.control_mode = ControlMode::UartGpioInterlock;
        } else {
            result.error = "invalid_gimbal_control_mode";
            return result;
        }
        result.uart_device = config.value("uart_device", "/dev/ttySTM3");
        if (result.uart_device.empty()) {
            result.error = "gimbal_uart_device_required";
            return result;
        }
        result.motion_verified = config.value("motion_verified", false);
        if (config.contains("safe_window_offsets_deg") &&
            config["safe_window_offsets_deg"].is_object()) {
            const auto& window = config["safe_window_offsets_deg"];
            result.limits.yaw_min_offset_deg = numberOrNone(window, "yaw_min");
            result.limits.yaw_max_offset_deg = numberOrNone(window, "yaw_max");
            result.limits.pitch_min_offset_deg = numberOrNone(window, "pitch_min");
            result.limits.pitch_max_offset_deg = numberOrNone(window, "pitch_max");
        }
        const auto nudge = numberOrNone(config, "small_nudge_deg");
        result.precheck.relative_to_alignment = false;
        result.precheck.center = Waypoint{0.0, 0.0};
        result.precheck.nudge_step_deg = nudge.value_or(5.0);

        if (config.contains("search") && config["search"].is_object()) {
            const auto& search = config["search"];
            const double yaw_extent = search.value("yaw_extent_deg", 35.0);
            const double pitch_extent = search.value("pitch_extent_deg", 25.0);
            const double speed = search.value("profile_min_speed_deg_s", 20.0);
            result.precheck.initial_observe_ms =
                search.value("initial_observe_ms", 600LL);
            result.precheck.waypoint_period_ms =
                search.value("waypoint_observe_ms", 350LL);
            result.precheck.stop_queueing_ms =
                search.value("stop_queueing_ms", 23000LL);
            result.precheck.hard_deadline_ms =
                search.value("hard_deadline_ms", 24000LL);
            result.precheck.visual_fresh_ms =
                search.value("visual_fresh_ms", 600LL);
            result.precheck.subject_stable_ms =
                search.value("subject_stable_ms", 300LL);
            result.precheck.subject_observations =
                search.value("subject_observations", 2U);
            result.precheck.candidate_centering_ms =
                search.value("candidate_centering_ms", 3000LL);
            result.precheck.framing_window_ms =
                search.value("framing_window_ms", 1200LL);
            result.precheck.framing_pass_score =
                search.value("framing_pass_score", 0.65);
            result.precheck.settle_buffer_ms =
                search.value("settle_buffer_ms", 200LL);
            result.precheck.manual_lease_ttl_ms =
                search.value("manual_lease_ttl_ms", 30000LL);
            result.precheck.timing_source =
                search.value("timing_source", std::string{});

            if (yaw_extent <= 0.0 || yaw_extent > 70.0 ||
                pitch_extent <= 0.0 || pitch_extent > 70.0 || speed <= 0.0 ||
                result.precheck.timing_source.empty()) {
                result.error = "invalid_gimbal_search_geometry";
                return result;
            }
            double previous_yaw = 0.0;
            double previous_pitch = 0.0;
            const std::vector<std::pair<double, double>> primary = {
                {-yaw_extent, 0.0}, {yaw_extent, 0.0},
                {0.0, -pitch_extent}, {0.0, pitch_extent}};
            for (const auto& target : primary) {
                result.precheck.search_waypoints.push_back({
                    target.first, target.second,
                    continuousDeadlineMs(
                        std::abs(target.first - previous_yaw),
                        std::abs(target.second - previous_pitch), speed)});
                previous_yaw = target.first;
                previous_pitch = target.second;
            }
        }

        if (result.precheck.search_waypoints.empty() &&
            config.contains("waypoint_offsets") &&
            config["waypoint_offsets"].is_object()) {
            const auto& offsets = config["waypoint_offsets"];
            const auto left = numberOrNone(offsets, "left");
            const auto right = numberOrNone(offsets, "right");
            const auto up = numberOrNone(offsets, "up");
            const auto down = numberOrNone(offsets, "down");
            if (left && right && up && down) {
                result.precheck.search_waypoints = {
                    {*left, 0.0}, {*right, 0.0}, {0.0, *down}, {0.0, *up}
                };
            }
        }
        if (config.contains("tracking") && config["tracking"].is_object()) {
            const auto& tracking = config["tracking"];
            result.tracking.target_x = tracking.value("target_x", result.tracking.target_x);
            result.tracking.target_y = tracking.value("target_y", result.tracking.target_y);
            result.tracking.deadband_x = tracking.value("deadband_x", result.tracking.deadband_x);
            result.tracking.deadband_y = tracking.value("deadband_y", result.tracking.deadband_y);
            result.tracking.yaw_gain_deg = tracking.value("yaw_gain_deg", result.tracking.yaw_gain_deg);
            result.tracking.pitch_gain_deg = tracking.value("pitch_gain_deg", result.tracking.pitch_gain_deg);
            result.tracking.maximum_step_deg = tracking.value("maximum_step_deg", result.tracking.maximum_step_deg);
            result.tracking.smoothing_alpha = tracking.value("smoothing_alpha", result.tracking.smoothing_alpha);
            result.tracking.yaw_direction = tracking.value("yaw_direction", result.tracking.yaw_direction);
            result.tracking.pitch_direction = tracking.value("pitch_direction", result.tracking.pitch_direction);
        }
        if (!result.motion_verified) result.error = "motion_not_verified";
        else if (!result.limits.verified()) result.error = "motion_limits_unverified";
        else if (!result.precheck.verified()) result.error = "precheck_geometry_unverified";
        else if (!result.tracking.verified()) result.error = "tracking_config_unverified";
    } catch (const std::exception& error) {
        result.error = std::string("invalid_gimbal_board_config:") + error.what();
    }
    return result;
}

}  // namespace edgefocus::gimbal
