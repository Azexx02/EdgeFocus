#pragma once
#include "vision/VisionResult.h"
#include <cstdint>

namespace edgefocus {

enum class MockScenario : uint8_t {
    FOCUSED     = 0,
    HEAD_DOWN   = 1,
    EYES_CLOSED = 2,
    AWAY        = 3,
    PHONE_USE   = 4,
    LOW_QUALITY = 5,
    LOW_CONF    = 6
};

class MockVisionSource {
public:
    MockVisionSource() = default;

    void setScenario(MockScenario scenario) { scenario_ = scenario; }
    MockScenario scenario() const { return scenario_; }

    VisionResult buildMockResult() const;

private:
    MockScenario scenario_ = MockScenario::FOCUSED;
};

} // namespace edgefocus
