#include "platform/gimbal/GimbalHardwareService.h"
#include "platform/gimbal/GimbalProtocol.h"
#include "platform/gimbal/LinuxGimbalIo.h"
#include "tools/GimbalOnewayStopDiagnostic.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using edgefocus::gimbal::CommandResult;
using edgefocus::gimbal::GimbalHardwareService;
using edgefocus::gimbal::LinuxGimbalIo;
using edgefocus::gimbal::MaintenanceOperation;
using edgefocus::gimbal::MaintenanceRequest;
using edgefocus::gimbal::CurrentProtectionConfig;
using edgefocus::gimbal::diagnostic::GimbalOnewayStopDiagnostic;

constexpr const char* kDefaultConfig = "/opt/edgefocus/config/gimbal-board.json";

void printUsage() {
    std::cerr
        << "Usage:\n"
        << "  gimbal_bringup probe [--config PATH]\n"
        << "  gimbal_bringup align --allow-motion --confirm-board [--config PATH]\n"
        << "  gimbal_bringup move --axis yaw|pitch --delta-deg N "
           "--allow-motion --confirm-board [--config PATH]\n"
        << "  gimbal_bringup stop [--config PATH]\n"
        << "  gimbal_bringup recovery-watch [--config PATH]\n"
        << "  gimbal_bringup calibrate-electrical --allow-motion --confirm-board [--config PATH]\n"
        << "  gimbal_bringup capture-origin --confirm-origin-capture [--config PATH]\n"
        << "  gimbal_bringup current-sample --allow-motion --confirm-board [--config PATH]\n"
        << "  gimbal_bringup maintenance-move --axis yaw|pitch --delta-deg N "
           "--allow-motion --confirm-board [--config PATH]\n"
        << "  gimbal_bringup protect-current-set --p-peak N --p-avg N --y-peak N --y-avg N "
           "--stall-ms N --confirm-current-protection [--config PATH]\n"
        << "  gimbal_bringup nvm-clear-origin|nvm-clear-all --confirm-nvm-clear "
           "--confirm-second [--config PATH]\n"
        << "  gimbal_bringup arm-production --allow-motion --confirm-board [--config PATH]\n"
        << "  gimbal_bringup oneway-stop --allow-motion --confirm-board "
           "--diagnose-no-rx\n"
        << "  gimbal_bringup oneway-pitch5 --allow-motion --confirm-board "
           "--diagnose-no-rx --confirm-absolute-angle\n";
}

std::optional<double> numberOrNone(const nlohmann::json& value, const char* key) {
    if (!value.contains(key) || !value.at(key).is_number()) return std::nullopt;
    return value.at(key).get<double>();
}

GimbalHardwareService::MotionLimits loadLimits(const std::string& path) {
    GimbalHardwareService::MotionLimits limits;
    std::ifstream input(path);
    if (!input) return limits;
    nlohmann::json config;
    input >> config;
    if (!config.value("motion_verified", false)) return limits;
    if (!config.contains("safe_window_offsets_deg") ||
        !config.at("safe_window_offsets_deg").is_object()) return limits;
    const auto& window = config.at("safe_window_offsets_deg");
    limits.yaw_min_offset_deg = numberOrNone(window, "yaw_min");
    limits.yaw_max_offset_deg = numberOrNone(window, "yaw_max");
    limits.pitch_min_offset_deg = numberOrNone(window, "pitch_min");
    limits.pitch_max_offset_deg = numberOrNone(window, "pitch_max");
    return limits;
}

LinuxGimbalIo::Options loadIoOptions(const std::string& path) {
    LinuxGimbalIo::Options options;
    std::ifstream input(path);
    if (!input) return options;
    nlohmann::json config;
    input >> config;
    options.uart_device = config.value("uart_device", options.uart_device);
    const std::string mode = config.value("control_mode", "uart_only");
    if (mode == "uart_gpio_interlock") {
        options.control_mode = edgefocus::gimbal::ControlMode::UartGpioInterlock;
    } else {
        options.control_mode = edgefocus::gimbal::ControlMode::UartOnly;
    }
    return options;
}

bool containsArg(int argc, char** argv, const std::string& expected) {
    for (int i = 2; i < argc; ++i) {
        if (argv[i] == expected) return true;
    }
    return false;
}

std::string valueAfter(int argc, char** argv, const std::string& key,
                       const std::string& fallback = {}) {
    for (int i = 2; i + 1 < argc; ++i) {
        if (argv[i] == key) return argv[i + 1];
    }
    return fallback;
}

bool motionConfirmed(int argc, char** argv) {
    return containsArg(argc, argv, "--allow-motion") &&
           containsArg(argc, argv, "--confirm-board");
}

std::optional<std::uint16_t> uint16After(int argc, char** argv,
                                         const std::string& key) {
    try {
        const auto text = valueAfter(argc, argv, key);
        if (text.empty()) return std::nullopt;
        const auto value = std::stoul(text);
        if (value > 65535) return std::nullopt;
        return static_cast<std::uint16_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

int report(const char* operation, const CommandResult& result,
           const GimbalHardwareService& service,
           const nlohmann::json& details = nlohmann::json::object()) {
    const auto status = service.status();
    const auto snapshot = service.snapshot();
    nlohmann::json output = {
        {"operation", operation},
        {"success", result.success},
        {"timeout", result.timeout},
        {"error", result.error},
        {"failed_steps", result.failed_steps},
        {"uart_transcript", result.transcript},
        {"state", edgefocus::gimbal::stateName(status.state)},
        {"fault_active", status.fault_active},
        {"recoverable", status.recoverable},
        {"fault_latched_reason", status.fault_latched_reason},
        {"yaw_deg", status.yaw_deg ? nlohmann::json(*status.yaw_deg) : nlohmann::json(nullptr)},
        {"pitch_deg",
         status.pitch_deg ? nlohmann::json(*status.pitch_deg) : nlohmann::json(nullptr)},
        {"protocol", snapshot.protocol_version},
        {"arm_profile", snapshot.arm_profile == edgefocus::gimbal::ArmProfile::Production
                            ? "PRODUCTION"
                            : (snapshot.arm_profile == edgefocus::gimbal::ArmProfile::Maintenance
                                   ? "MAINTENANCE"
                                   : "NONE")},
        {"calibration_valid", snapshot.calibration_valid},
        {"origin_valid", snapshot.origin_valid},
        {"calibration_epoch", snapshot.calibration_epoch},
        {"origin_epoch", snapshot.origin_epoch},
        {"current_protection", snapshot.current_protection_state},
        {"maintenance_operation", snapshot.operation},
        {"maintenance_operation_generation", snapshot.operation_generation
            ? nlohmann::json(*snapshot.operation_generation) : nlohmann::json(nullptr)},
        {"maintenance_progress", snapshot.operation_progress
            ? nlohmann::json(*snapshot.operation_progress) : nlohmann::json(nullptr)},
        {"last_operation_status", snapshot.last_operation_status},
    };
    if (result.current) {
        output["current"] = {
            {"pitch_raw", result.current->pitch_raw},
            {"pitch_average", result.current->pitch_average},
            {"pitch_peak", result.current->pitch_peak},
            {"yaw_raw", result.current->yaw_raw},
            {"yaw_average", result.current->yaw_average},
            {"yaw_peak", result.current->yaw_peak},
            {"window_ms", result.current->window_ms},
            {"samples", result.current->samples},
            {"valid", result.current->valid},
            {"saturated", result.current->saturated},
            {"sample_age_ms", result.current->sample_age_ms},
        };
    }
    if (!details.empty()) output["details"] = details;
    std::cout << output.dump(2) << '\n';
    return result.success ? 0 : 2;
}

CommandResult finishMotion(CommandResult motion_result,
                           GimbalHardwareService& service) {
    auto stop_result = service.stopAndDisable();
    motion_result.transcript.insert(motion_result.transcript.end(),
                                    stop_result.transcript.begin(),
                                    stop_result.transcript.end());
    motion_result.failed_steps.insert(motion_result.failed_steps.end(),
                                      stop_result.failed_steps.begin(),
                                      stop_result.failed_steps.end());
    motion_result.timeout = motion_result.timeout || stop_result.timeout;
    if (!stop_result.success) {
        motion_result.success = false;
        motion_result.failed_steps.push_back("stop_and_disable");
        const std::string stop_error = stop_result.error.empty()
                                           ? "stop_and_disable_failed"
                                           : stop_result.error;
        if (motion_result.error.empty()) {
            motion_result.error = stop_error;
        } else {
            motion_result.error += "; " + stop_error;
        }
    }
    return motion_result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 64;
    }
    const std::string command = argv[1];
    if (command == "oneway-stop" || command == "oneway-pitch5") {
        const bool pitch_five = command == "oneway-pitch5";
        if (!motionConfirmed(argc, argv) ||
            !containsArg(argc, argv, "--diagnose-no-rx") ||
            (pitch_five && !containsArg(argc, argv, "--confirm-absolute-angle"))) {
            std::cerr << "One-way diagnostic refused: --allow-motion, "
                         "--confirm-board, and --diagnose-no-rx are required; "
                         "PITCH 5 also requires --confirm-absolute-angle\n";
            return 65;
        }
        LinuxGimbalIo io;
        GimbalOnewayStopDiagnostic diagnostic(io);
        const auto result = pitch_five ? diagnostic.runPitchFive() : diagnostic.run();
        const nlohmann::json output = {
            {"operation", command},
            {"sequence_completed", result.sequence_completed},
            {"error", result.error},
            {"fault_active", result.fault_active},
            {"commands_sent", result.commands_sent},
            {"uart_transcript", result.uart_transcript},
            {"human_observation_required", true},
            {"control_confirmed", false},
        };
        std::cout << output.dump(2) << '\n';
        return result.sequence_completed ? 0 : 2;
    }
    const std::string config_path = valueAfter(argc, argv, "--config", kDefaultConfig);
    auto limits = loadLimits(config_path);
    auto io = std::make_unique<LinuxGimbalIo>(loadIoOptions(config_path));
    GimbalHardwareService service(std::move(io), limits);
    std::string error;
    if (!service.start(&error)) {
        std::cerr << "gimbal_bringup: " << error << '\n';
        return 1;
    }

    if (command == "probe") {
        const auto result = service.probeDiagnostics();
        service.stop();
        return report("probe", result, service);
    }
    if (command == "stop") {
        const auto result = service.stopAndDisable();
        service.stop();
        return report("stop", result, service);
    }
    if (command == "recovery-watch") {
        const auto result = service.probeRecovery();
        service.stop();
        return report("recovery-watch", result, service);
    }
    if (command == "calibrate-electrical") {
        if (!motionConfirmed(argc, argv)) {
            std::cerr << "Calibration refused: --allow-motion and --confirm-board are required\n";
            service.stop();
            return 65;
        }
        auto result = service.executeMaintenance({MaintenanceOperation::Probe});
        if (result.success) {
            result = service.executeMaintenance(
                {MaintenanceOperation::CalibrateElectricalAll}, 5000);
        }
        if (result.success) {
            const auto accepted_generation = result.motion_generation;
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(60);
            while (std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                const auto poll = service.refreshAngle(1000);
                if (!poll.success) {
                    result = poll;
                    break;
                }
                const auto snapshot = service.snapshot();
                const bool same_operation = accepted_generation &&
                    snapshot.operation_generation == accepted_generation;
                if (!same_operation && snapshot.calibration_valid) break;
                if (snapshot.last_operation_status == "FAILED" ||
                    snapshot.last_operation_status == "ABORTED") {
                    result.success = false;
                    result.error = "electrical_calibration_failed";
                    break;
                }
            }
            if (result.success && !service.snapshot().calibration_valid) {
                result.success = false;
                result.timeout = true;
                result.error = "electrical_calibration_timeout";
            }
        }
        result = finishMotion(std::move(result), service);
        const int exit_code = report(command.c_str(), result, service);
        service.stop();
        return exit_code;
    }
    if (command == "capture-origin") {
        if (!containsArg(argc, argv, "--confirm-origin-capture")) {
            std::cerr << "Origin capture refused: --confirm-origin-capture is required\n";
            service.stop();
            return 65;
        }
        auto result = service.executeMaintenance({MaintenanceOperation::Probe});
        if (result.success) {
            result = service.executeMaintenance({MaintenanceOperation::CaptureOrigin});
        }
        result = finishMotion(std::move(result), service);
        const int exit_code = report(command.c_str(), result, service);
        service.stop();
        return exit_code;
    }
    if (command == "maintenance-move") {
        if (!motionConfirmed(argc, argv)) {
            std::cerr << "Maintenance move refused: --allow-motion and "
                         "--confirm-board are required\n";
            service.stop();
            return 65;
        }
        const std::string axis = valueAfter(argc, argv, "--axis");
        const std::string delta_text = valueAfter(argc, argv, "--delta-deg");
        double delta = 0.0;
        try {
            delta = std::stod(delta_text);
        } catch (...) {
            std::cerr << "Maintenance move requires a numeric --delta-deg\n";
            service.stop();
            return 64;
        }
        if ((axis != "yaw" && axis != "pitch") || delta == 0.0 ||
            std::abs(delta) > 3.0) {
            std::cerr << "Maintenance move requires yaw|pitch and a non-zero "
                         "delta no larger than 3 degrees\n";
            service.stop();
            return 64;
        }

        CommandResult result;
        std::vector<std::string> transcript;
        auto adopt = [&](CommandResult next) {
            transcript.insert(transcript.end(), next.transcript.begin(),
                              next.transcript.end());
            next.transcript = transcript;
            result = std::move(next);
            return result.success;
        };

        bool ok = adopt(service.executeMaintenance({MaintenanceOperation::Probe}));
        if (ok) ok = adopt(service.executeMaintenance(
            {MaintenanceOperation::ArmMaintenance}));
        if (ok) ok = adopt(service.executeMaintenance({MaintenanceOperation::Hold}));
        auto sample_started = std::chrono::steady_clock::now();
        if (ok) {
            sample_started = std::chrono::steady_clock::now();
            ok = adopt(service.executeMaintenance({MaintenanceOperation::CurrentReset}));
        }
        if (ok) ok = adopt(service.refreshAngle(1000));

        std::optional<double> start_yaw;
        std::optional<double> start_pitch;
        std::optional<double> target_yaw;
        std::optional<double> target_pitch;
        std::optional<double> final_yaw;
        std::optional<double> final_pitch;
        std::optional<std::uint32_t> motion_generation;
        bool motion_settled = false;
        if (ok) {
            const auto status = service.status();
            start_yaw = status.yaw_deg;
            start_pitch = status.pitch_deg;
            if (!start_yaw || !start_pitch) {
                result.success = false;
                result.error = "maintenance_angle_unknown";
                result.error_code = "SAMPLE_STALE";
                ok = false;
            }
        }
        if (ok) {
            target_yaw = axis == "yaw"
                ? edgefocus::gimbal::offsetAngle(*start_yaw, delta) : *start_yaw;
            target_pitch = axis == "pitch"
                ? edgefocus::gimbal::offsetAngle(*start_pitch, delta) : *start_pitch;
            const bool inside_maintenance_window =
                std::abs(edgefocus::gimbal::shortestAngleDelta(0.0, *target_yaw)) <= 10.0 &&
                std::abs(edgefocus::gimbal::shortestAngleDelta(0.0, *target_pitch)) <= 10.0;
            if (!inside_maintenance_window) {
                result.success = false;
                result.error = "maintenance_target_out_of_bounds";
                result.error_code = "TARGET_OUT_OF_BOUNDS";
                ok = false;
            }
        }
        if (ok) {
            ok = adopt(service.moveToVerified(*target_yaw, *target_pitch, 5000, 1.0));
            motion_generation = result.motion_generation;
            motion_settled = result.settled;
            const auto status = service.status();
            final_yaw = status.yaw_deg;
            final_pitch = status.pitch_deg;
        }
        if (ok) {
            const auto elapsed = std::chrono::steady_clock::now() - sample_started;
            if (elapsed < std::chrono::milliseconds(1100)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1100) - elapsed);
            }
            ok = adopt(service.executeMaintenance({MaintenanceOperation::CurrentRead}));
            result.motion_generation = motion_generation;
            result.settled = motion_settled;
        }
        if (ok && (!result.current || !result.current->valid ||
                   result.current->saturated || result.current->window_ms < 1000)) {
            result.success = false;
            result.error = "current_sample_not_usable";
            result.error_code = "CURRENT_SAMPLE_INVALID";
        }

        const nlohmann::json details = {
            {"axis", axis}, {"delta_deg", delta},
            {"start_yaw_deg", start_yaw ? nlohmann::json(*start_yaw) : nlohmann::json(nullptr)},
            {"start_pitch_deg", start_pitch ? nlohmann::json(*start_pitch) : nlohmann::json(nullptr)},
            {"target_yaw_deg", target_yaw ? nlohmann::json(*target_yaw) : nlohmann::json(nullptr)},
            {"target_pitch_deg", target_pitch ? nlohmann::json(*target_pitch) : nlohmann::json(nullptr)},
            {"final_yaw_deg", final_yaw ? nlohmann::json(*final_yaw) : nlohmann::json(nullptr)},
            {"final_pitch_deg", final_pitch ? nlohmann::json(*final_pitch) : nlohmann::json(nullptr)},
        };
        result = finishMotion(std::move(result), service);
        const int exit_code = report(command.c_str(), result, service, details);
        service.stop();
        return exit_code;
    }
    if (command == "current-sample") {
        if (!motionConfirmed(argc, argv)) {
            std::cerr << "Current sampling refused: --allow-motion and --confirm-board are required\n";
            service.stop();
            return 65;
        }
        auto result = service.executeMaintenance({MaintenanceOperation::ArmMaintenance});
        if (result.success) result = service.executeMaintenance({MaintenanceOperation::Hold});
        auto sample_started = std::chrono::steady_clock::now();
        if (result.success) {
            sample_started = std::chrono::steady_clock::now();
            result = service.executeMaintenance({MaintenanceOperation::CurrentReset});
        }
        if (result.success) {
            result = service.executeMaintenance(
                {MaintenanceOperation::VerifyMotionPattern}, 30000);
        }
        if (result.success) {
            const auto elapsed = std::chrono::steady_clock::now() - sample_started;
            if (elapsed < std::chrono::milliseconds(1100)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1100) - elapsed);
            }
            result = service.executeMaintenance({MaintenanceOperation::CurrentRead});
        }
        if (result.success && (!result.current || !result.current->valid ||
                               result.current->saturated ||
                               result.current->window_ms < 1000)) {
            result.success = false;
            result.error = "current_sample_not_usable";
        }
        result = finishMotion(std::move(result), service);
        const int exit_code = report(command.c_str(), result, service);
        service.stop();
        return exit_code;
    }
    if (command == "protect-current-set") {
        if (!containsArg(argc, argv, "--confirm-current-protection")) {
            std::cerr << "Current protection write refused: confirmation is required\n";
            service.stop();
            return 65;
        }
        const auto p_peak = uint16After(argc, argv, "--p-peak");
        const auto p_avg = uint16After(argc, argv, "--p-avg");
        const auto y_peak = uint16After(argc, argv, "--y-peak");
        const auto y_avg = uint16After(argc, argv, "--y-avg");
        const auto stall = uint16After(argc, argv, "--stall-ms");
        if (!p_peak || !p_avg || !y_peak || !y_avg || !stall) {
            std::cerr << "All current protection thresholds must be valid uint16 values\n";
            service.stop();
            return 64;
        }
        auto result = service.executeMaintenance({MaintenanceOperation::Probe});
        if (result.success) {
            MaintenanceRequest request{MaintenanceOperation::ProtectCurrentSet};
            request.protection = CurrentProtectionConfig{
                *p_peak, *p_avg, *y_peak, *y_avg, *stall};
            result = service.executeMaintenance(request);
        }
        const int exit_code = report(command.c_str(), result, service);
        service.stop();
        return exit_code;
    }
    if (command == "nvm-clear-origin" || command == "nvm-clear-all") {
        if (!containsArg(argc, argv, "--confirm-nvm-clear") ||
            !containsArg(argc, argv, "--confirm-second")) {
            std::cerr << "NVM clear refused: both confirmation flags are required\n";
            service.stop();
            return 65;
        }
        auto result = service.executeMaintenance({MaintenanceOperation::Probe});
        if (result.success) {
            result = service.executeMaintenance({
                command == "nvm-clear-all" ? MaintenanceOperation::ClearAll
                                           : MaintenanceOperation::ClearOrigin});
        }
        const int exit_code = report(command.c_str(), result, service);
        service.stop();
        return exit_code;
    }
    if (command == "arm-production") {
        if (!motionConfirmed(argc, argv)) {
            std::cerr << "Production arm refused: --allow-motion and --confirm-board are required\n";
            service.stop();
            return 65;
        }
        auto result = service.executeMaintenance({MaintenanceOperation::ArmProduction});
        result = finishMotion(std::move(result), service);
        const int exit_code = report(command.c_str(), result, service);
        service.stop();
        return exit_code;
    }
    if (command != "align" && command != "move") {
        printUsage();
        service.stop();
        return 64;
    }
    if (!motionConfirmed(argc, argv)) {
        std::cerr << "Motion refused: --allow-motion and --confirm-board are both required\n";
        service.stop();
        return 65;
    }

    auto result = service.enableAndAlign();
    if (result.success && command == "move") {
        if (!limits.verified()) {
            result = {false, false, "motion_limits_unverified"};
        } else {
            const std::string axis = valueAfter(argc, argv, "--axis");
            const std::string delta_text = valueAfter(argc, argv, "--delta-deg");
            try {
                const double delta = std::stod(delta_text);
                const auto status = service.status();
                if (!status.yaw_deg || !status.pitch_deg) {
                    result = {false, false, "current_angle_unknown"};
                } else if (axis == "yaw") {
                    result = service.moveTo(*status.yaw_deg + delta, *status.pitch_deg);
                } else if (axis == "pitch") {
                    result = service.moveTo(*status.yaw_deg, *status.pitch_deg + delta);
                } else {
                    result = {false, false, "axis_must_be_yaw_or_pitch"};
                }
            } catch (...) {
                result = {false, false, "invalid_delta_deg"};
            }
        }
    }
    result = finishMotion(std::move(result), service);
    const int exit_code = report(command.c_str(), result, service);
    service.stop();
    return exit_code;
}
