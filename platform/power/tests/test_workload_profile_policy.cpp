#include "platform/power/WorkloadProfilePolicy.h"

#include <cassert>
#include <iostream>

using namespace edgefocus::power;

int main() {
    WorkloadProfilePolicy policy;
    auto status = policy.snapshot();
    assert(status.effective_profile == WorkloadProfile::Go);
    assert(status.source == ProfileSource::AutoFailsafe);

    policy.setManualOverride(WorkloadProfile::Home);
    policy.updateExternalPower(ExternalPowerState::Unknown);
    status = policy.snapshot();
    assert(status.manual_override_active);
    assert(status.effective_profile == WorkloadProfile::Home);

    // First confirmed fact establishes the baseline and does not clear a
    // manual override selected during startup UNKNOWN.
    policy.updateExternalPower(ExternalPowerState::Absent);
    status = policy.snapshot();
    assert(status.manual_override_active);
    assert(status.power_epoch == 0);

    policy.updateExternalPower(ExternalPowerState::Unknown);
    assert(policy.snapshot().manual_override_active);
    policy.updateExternalPower(ExternalPowerState::Absent);
    assert(policy.snapshot().manual_override_active);
    assert(policy.snapshot().power_epoch == 0);

    policy.updateExternalPower(ExternalPowerState::Present);
    status = policy.snapshot();
    assert(!status.manual_override_active);
    assert(status.power_epoch == 1);
    assert(status.effective_profile == WorkloadProfile::Home);
    assert(status.source == ProfileSource::AutoExternal);

    policy.setManualOverride(WorkloadProfile::Go);
    policy.updateExternalPower(ExternalPowerState::Unknown);
    assert(policy.snapshot().effective_profile == WorkloadProfile::Go);
    assert(policy.snapshot().manual_override_active);
    policy.updateExternalPower(ExternalPowerState::Absent);
    status = policy.snapshot();
    assert(!status.manual_override_active);
    assert(status.power_epoch == 2);
    assert(status.effective_profile == WorkloadProfile::Go);
    assert(status.source == ProfileSource::AutoNoExternal);

    std::cout << "workload profile policy tests passed\n";
    return 0;
}
