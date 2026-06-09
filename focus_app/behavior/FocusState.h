#pragma once
#include <cstdint>

namespace edgefocus {

enum class FocusState : uint8_t {
    FOCUSED = 0,
    LOOKING_AWAY = 1,
    SLEEPY_OR_SLUMPED = 2,
    PHONE_IN_USE = 3,

    // Internal / Auxiliary states
    AWAY = 4,
    LOW_CONFIDENCE = 5,
    PAUSED = 6,
    CALIBRATING = 7
};

} // namespace edgefocus
