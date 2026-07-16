#include "platform/power/WorkloadProfilePolicy.h"

namespace edgefocus::power {

void WorkloadProfilePolicy::updateExternalPower(ExternalPowerState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.external_power_state = state;
    if (state != ExternalPowerState::Unknown) {
        if (last_confirmed_ && *last_confirmed_ != state) {
            ++snapshot_.power_epoch;
            manual_override_.reset();
        }
        last_confirmed_ = state;
    }
    recomputeLocked();
}

void WorkloadProfilePolicy::setManualOverride(WorkloadProfile profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    manual_override_ = profile;
    recomputeLocked();
}

void WorkloadProfilePolicy::clearManualOverride() {
    std::lock_guard<std::mutex> lock(mutex_);
    manual_override_.reset();
    recomputeLocked();
}

ProfileSnapshot WorkloadProfilePolicy::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void WorkloadProfilePolicy::recomputeLocked() {
    snapshot_.manual_override_active = manual_override_.has_value();
    if (manual_override_) {
        snapshot_.effective_profile = *manual_override_;
        snapshot_.source = ProfileSource::Manual;
        return;
    }
    switch (snapshot_.external_power_state) {
        case ExternalPowerState::Present:
            snapshot_.effective_profile = WorkloadProfile::Home;
            snapshot_.source = ProfileSource::AutoExternal;
            break;
        case ExternalPowerState::Absent:
            snapshot_.effective_profile = WorkloadProfile::Go;
            snapshot_.source = ProfileSource::AutoNoExternal;
            break;
        case ExternalPowerState::Unknown:
            snapshot_.effective_profile = WorkloadProfile::Go;
            snapshot_.source = ProfileSource::AutoFailsafe;
            break;
    }
}

}  // namespace edgefocus::power
