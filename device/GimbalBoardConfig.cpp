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
        3000.0,
        std::max(yaw_distance, pitch_distance) / speed_deg_s * 1000.0 +
            1500.0));
}

double positiveCircularDistance(double from, double to) {
    double distance = std::fmod(to - from, 360.0);
    if (distance < 0.0) distance += 360.0;
    return distance;
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
        if (config.value("schema_version", 0) != 2) {
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
        result.precheck.relative_to_alignment = true;
        result.precheck.center = Waypoint{0.0, 0.0};
        if (nudge) result.precheck.nudge_step_deg = *nudge;

        if (config.contains("search") && config["search"].is_object()) {
            const auto& search = config["search"];
            const double yaw_min = search.value("yaw_min", -45.0);
            const double yaw_max = search.value("yaw_max", 45.0);
            const double pitch_min = search.value("pitch_min", -20.0);
            const double pitch_max = search.value("pitch_max", 20.0);
            const double speed = search.value("verified_min_speed_deg_s", 10.0);
            const double vertical_fov =
                search.value("effective_vertical_fov_deg", 50.0);
            const double recovery_extent =
                search.value("full_pitch_recovery_extent_deg", 0.0);
            result.precheck.initial_observe_ms =
                search.value("initial_observe_ms", 1000LL);
            result.precheck.stop_queueing_ms =
                search.value("stop_queueing_ms", 72000LL);
            result.precheck.hard_deadline_ms =
                search.value("hard_deadline_ms", 75000LL);
            result.precheck.visual_fresh_ms =
                search.value("visual_fresh_ms", 600LL);
            result.precheck.candidate_centering_ms =
                search.value("candidate_centering_ms", 3000LL);
            result.precheck.framing_window_ms =
                search.value("framing_window_ms", 1200LL);
            result.precheck.framing_pass_score =
                search.value("framing_pass_score", 0.65);

            if (yaw_min >= 0.0 || yaw_max <= 0.0 ||
                pitch_min >= 0.0 || pitch_max <= 0.0 || speed <= 0.0 ||
                vertical_fov <= 0.0 || recovery_extent <= 90.0 ||
                recovery_extent >= 180.0) {
                result.error = "invalid_gimbal_search_geometry";
                return result;
            }
            double previous_yaw = 0.0;
            double previous_pitch = 0.0;
            const std::vector<std::pair<double, double>> primary = {
                {yaw_min, 0.0}, {yaw_max, 0.0},
                {yaw_max, pitch_max}, {yaw_min, pitch_max},
                {yaw_min, pitch_min}, {yaw_max, pitch_min},
                {0.0, 0.0}};
            for (const auto& target : primary) {
                result.precheck.search_waypoints.push_back({
                    target.first, target.second,
                    continuousDeadlineMs(
                        std::abs(target.first - previous_yaw),
                        std::abs(target.second - previous_pitch), speed)});
                previous_yaw = target.first;
                previous_pitch = target.second;
            }

            const double spacing_limit = vertical_fov * 0.80;
            const auto half_steps = static_cast<std::size_t>(
                std::ceil(recovery_extent / spacing_limit));
            if (half_steps == 0) {
                result.error = "invalid_gimbal_recovery_geometry";
                return result;
            }
            std::vector<double> recovery{0.0};
            for (std::size_t step = 1; step <= half_steps; ++step) {
                recovery.push_back(
                    recovery_extent * static_cast<double>(step) /
                    static_cast<double>(half_steps));
            }
            recovery.push_back(-recovery_extent);
            for (std::size_t step = half_steps - 1; step > 0; --step) {
                recovery.push_back(
                    -recovery_extent * static_cast<double>(step) /
                    static_cast<double>(half_steps));
            }
            recovery.push_back(0.0);
            previous_pitch = 0.0;
            for (const double pitch : recovery) {
                const double distance =
                    positiveCircularDistance(previous_pitch, pitch);
                result.precheck.recovery_waypoints.push_back({
                    0.0, pitch,
                    continuousDeadlineMs(0.0, distance, speed)});
                previous_pitch = pitch;
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
                    {*left, 0.0}, {*right, 0.0}, {0.0, *up}, {0.0, *down},
                    {*left, *up}, {*right, *up}, {*left, *down}, {*right, *down}
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
