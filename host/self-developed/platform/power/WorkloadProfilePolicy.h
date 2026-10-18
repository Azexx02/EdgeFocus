#pragma once

#include "platform/power/PowerTypes.h"

#include <mutex>
#include <optional>

namespace edgefocus::power {

class WorkloadProfilePolicy {
public:
    void updateExternalPower(ExternalPowerState state);
    void setManualOverride(WorkloadProfile profile);
    void clearManualOverride();
    ProfileSnapshot snapshot() const;

private:
    void recomputeLocked();

    mutable std::mutex mutex_;
    ProfileSnapshot snapshot_;
    std::optional<ExternalPowerState> last_confirmed_;
    std::optional<WorkloadProfile> manual_override_;
};

}  // namespace edgefocus::power
