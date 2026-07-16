#include "vision/VisionCadencePolicy.h"

#include <cassert>
#include <iostream>

using edgefocus::power::WorkloadProfile;
using edgefocus::session::Phase;
using edgefocus::vision::inferenceIntervalMs;

int main() {
    assert(inferenceIntervalMs(Phase::Precheck, WorkloadProfile::Home) == 250);
    assert(inferenceIntervalMs(Phase::Precheck, WorkloadProfile::Go) == 250);
    assert(inferenceIntervalMs(Phase::Focusing, WorkloadProfile::Home) == 250);
    assert(inferenceIntervalMs(Phase::Focusing, WorkloadProfile::Go) == 333);
    assert(inferenceIntervalMs(Phase::Paused, WorkloadProfile::Home) == 1000);
    assert(inferenceIntervalMs(Phase::Paused, WorkloadProfile::Go) == 1000);
    assert(inferenceIntervalMs(Phase::Idle, WorkloadProfile::Home) == 0);
    assert(inferenceIntervalMs(Phase::Ending, WorkloadProfile::Go) == 0);
    std::cout << "vision cadence policy tests passed\n";
    return 0;
}
