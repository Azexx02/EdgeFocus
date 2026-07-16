#include "platform/gimbal/GimbalHardwareService.h"
#include "platform/gimbal/LinuxGimbalIo.h"
#include "tools/GimbalOnewayStopDiagnostic.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using edgefocus::gimbal::CommandResult;
using edgefocus::gimbal::GimbalHardwareService;
using edgefocus::gimbal::LinuxGimbalIo;
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

int report(const char* operation, const CommandResult& result,
           const GimbalHardwareService& service) {
    const auto status = service.status();
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
    };
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
