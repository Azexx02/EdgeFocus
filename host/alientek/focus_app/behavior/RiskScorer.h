#pragma once
#include "behavior/EventDetector.h"

namespace edgefocus {

class RiskScorer {
public:
    int calculateScore(const FocusEvent& event);
};

} // namespace edgefocus
