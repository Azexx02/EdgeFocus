#pragma once

#include <cstdint>
#include <optional>

namespace edgefocus::power {

enum class ExternalPowerState : std::uint8_t { Present, Absent, Unknown };
enum class WorkloadProfile : std::uint8_t { Home, Go };
enum class ProfileSource : std::uint8_t {
    AutoExternal,
    AutoNoExternal,
    AutoFailsafe,
    Manual,
};

struct RawStatus {
    bool attach{};
    bool vbus_ready{};
    std::uint8_t port_status{};
    std::uint8_t monitoring_status{};
};

struct SourceSnapshot {
    ExternalPowerState state{ExternalPowerState::Unknown};
    std::optional<ExternalPowerState> last_confirmed_state;
    bool sample_available{};
    std::int64_t last_sample_ms{};
    std::int64_t last_success_ms{};
    std::int64_t state_since_ms{};
    unsigned consecutive_read_failures{};
};

struct ProfileSnapshot {
    ExternalPowerState external_power_state{ExternalPowerState::Unknown};
    WorkloadProfile effective_profile{WorkloadProfile::Go};
    ProfileSource source{ProfileSource::AutoFailsafe};
    bool manual_override_active{};
    std::uint64_t power_epoch{};
};

const char* externalPowerStateName(ExternalPowerState value) noexcept;
const char* workloadProfileName(WorkloadProfile value) noexcept;
const char* profileSourceName(ProfileSource value) noexcept;

}  // namespace edgefocus::power
