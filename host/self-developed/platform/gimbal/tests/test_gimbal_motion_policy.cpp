#include "platform/gimbal/GimbalMotionPolicy.h"

#include <cassert>

using namespace edgefocus::gimbal::motion_policy;

int main() {
    assert(normalizeAbsolute10(-1) == 3599);
    assert(signedOffset10(3599) == -1);
    assert(shortestDelta10(3500, 100) == 200);

    auto axis = resolveAutoAxis(0, 900);
    assert(axis.applied10 == 700);
    assert(axis.decision == AxisDecision::ClampedToBoundary);

    axis = resolveAutoAxis(800, 700);
    assert(axis.applied10 == 700);
    assert(axis.decision == AxisDecision::Pass);

    axis = resolveAutoAxis(800, 900);
    assert(axis.applied10 == 800);
    assert(axis.decision == AxisDecision::HeldOutward);

    axis = resolveAutoAxis(800, 1900); // Shortest path is still outward.
    assert(axis.applied10 == 800);
    assert(axis.decision == AxisDecision::HeldOutward);

    axis = resolveAutoAxis(2800, 2900); // -80 -> -70, toward center.
    assert(axis.applied10 == 2900);
    assert(axis.decision == AxisDecision::Pass);

    axis = resolveAutoAxis(800, 2600); // Exact 180 degrees.
    assert(axis.applied10 == 700);
    assert(axis.decision == AxisDecision::IntermediateTowardCenter);
    return 0;
}
