#pragma once

#include "device/GimbalPrecheckController.h"
#include "device/GimbalVisualTracker.h"
#include "platform/gimbal/GimbalHardwareService.h"

#include <string>

namespace edgefocus::gimbal {

struct GimbalBoardConfig {
    bool motion_verified{};
    ControlMode control_mode{ControlMode::UartOnly};
    std::string uart_device{"/dev/ttySTM3"};
    GimbalHardwareService::MotionLimits limits;
    PrecheckConfig precheck;
    TrackingConfig tracking;
    std::string error;

    bool readyForSession() const noexcept {
        return motion_verified && limits.verified() && precheck.verified() &&
               tracking.verified() && error.empty();
    }
};

GimbalBoardConfig loadGimbalBoardConfig(const std::string& path);

}  // namespace edgefocus::gimbal
