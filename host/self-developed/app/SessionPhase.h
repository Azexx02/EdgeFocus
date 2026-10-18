#pragma once

#include "SystemStateStore.h"

namespace edgefocus {

enum class SessionPhase {
    IDLE,
    PRECHECK,
    FOCUSING,
    PAUSED,
};

inline SessionPhase deriveSessionPhase(const SessionInfo& info) {
    if (!info.active) {
        return SessionPhase::IDLE;
    }
    if (info.paused) {
        return SessionPhase::PAUSED;
    }
    if (!info.calibration_completed) {
        return SessionPhase::PRECHECK;
    }
    return SessionPhase::FOCUSING;
}

inline const char* sessionPhaseToString(SessionPhase phase) {
    switch (phase) {
    case SessionPhase::IDLE:
        return "idle";
    case SessionPhase::PRECHECK:
        return "precheck";
    case SessionPhase::FOCUSING:
        return "focusing";
    case SessionPhase::PAUSED:
        return "paused";
    default:
        return "idle";
    }
}

} // namespace edgefocus
