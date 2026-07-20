#pragma once

#include "platform/gimbal/GimbalTypes.h"

namespace edgefocus::gimbal {

class IGimbalSessionHardware {
public:
    virtual ~IGimbalSessionHardware() = default;
    virtual Status status() const = 0;
    virtual CommandResult enableAndAlign(int timeout_ms = 10000) = 0;
    virtual CommandResult stopAndDisable(int timeout_ms = 1000) = 0;
    virtual CommandResult moveTo(double yaw_deg, double pitch_deg, int timeout_ms = 2000) = 0;
    virtual CommandResult moveToVerified(double yaw_deg, double pitch_deg,
                                         int timeout_ms = 8000,
                                         double tolerance_deg = 1.5) = 0;
    virtual CommandResult moveToManualVerified(double yaw_deg, double pitch_deg,
                                               int timeout_ms = 8000,
                                               double tolerance_deg = 1.5) = 0;
    virtual CommandResult rebaseAlignmentAtCurrent(int timeout_ms = 1000) = 0;
    virtual CommandResult trackTo(double yaw_deg, double pitch_deg, int timeout_ms = 500) = 0;
    virtual CommandResult hold(int timeout_ms = 1000) = 0;
    virtual CommandResult refreshAngle(int timeout_ms = 500) = 0;
    virtual CommandResult freezeAtCurrent(int timeout_ms = 2000) = 0;
    virtual CommandResult recover(int timeout_ms = 5000) = 0;
};

}  // namespace edgefocus::gimbal
