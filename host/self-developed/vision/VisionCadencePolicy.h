#pragma once

#include "platform/power/PowerTypes.h"
#include "session/SessionDomain.h"

#include <cstdint>

namespace edgefocus::vision {

inline constexpr std::int64_t inferenceIntervalMs(
    session::Phase phase,
    power::WorkloadProfile profile) noexcept {
    if (phase == session::Phase::Paused) return 1000;
    if (phase == session::Phase::Precheck) return 250;
    if (phase == session::Phase::Focusing) {
        return profile == power::WorkloadProfile::Home ? 250 : 333;
    }
    return 0;
}

}  // namespace edgefocus::vision
