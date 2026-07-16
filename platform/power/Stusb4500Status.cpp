#include "platform/power/Stusb4500Status.h"

namespace edgefocus::power {

ExternalPowerState decodeRawState(std::uint8_t port_status,
                                  std::uint8_t monitoring_status) noexcept {
    const bool attach = (port_status & 0x01U) != 0;
    const bool vbus_ready = (monitoring_status & 0x08U) != 0;
    if (attach && vbus_ready) return ExternalPowerState::Present;
    if (!attach && !vbus_ready) return ExternalPowerState::Absent;
    return ExternalPowerState::Unknown;
}

RawStatus decodeRawStatus(std::uint8_t port_status,
                          std::uint8_t monitoring_status) noexcept {
    return RawStatus{(port_status & 0x01U) != 0,
                     (monitoring_status & 0x08U) != 0,
                     port_status,
                     monitoring_status};
}

void SourceDebouncer::publish(ExternalPowerState state, std::int64_t now_ms) {
    if (snapshot_.state != state) {
        snapshot_.state = state;
        snapshot_.state_since_ms = now_ms;
    }
    if (state != ExternalPowerState::Unknown) snapshot_.last_confirmed_state = state;
}

SourceSnapshot SourceDebouncer::updateSuccess(std::uint8_t port_status,
                                              std::uint8_t monitoring_status,
                                              std::int64_t now_ms) {
    snapshot_.last_sample_ms = now_ms;
    snapshot_.last_success_ms = now_ms;
    snapshot_.sample_available = true;
    has_success_ = true;
    snapshot_.consecutive_read_failures = 0;
    const auto raw = decodeRawState(port_status, monitoring_status);
    if (raw == ExternalPowerState::Unknown) {
        candidate_.reset();
        candidate_count_ = 0;
        if (!raw_unknown_since_ms_) raw_unknown_since_ms_ = now_ms;
        if (now_ms - *raw_unknown_since_ms_ >= kUnknownTimeoutMs) {
            publish(ExternalPowerState::Unknown, now_ms);
        }
        return snapshot_;
    }

    raw_unknown_since_ms_.reset();
    if (candidate_ && *candidate_ == raw) {
        ++candidate_count_;
    } else {
        candidate_ = raw;
        candidate_count_ = 1;
    }
    if (candidate_count_ >= kConfirmSamples) publish(raw, now_ms);
    return snapshot_;
}

SourceSnapshot SourceDebouncer::updateFailure(std::int64_t now_ms) {
    snapshot_.last_sample_ms = now_ms;
    ++snapshot_.consecutive_read_failures;
    candidate_.reset();
    candidate_count_ = 0;
    if (snapshot_.consecutive_read_failures >= kFailureSamples ||
        !has_success_ ||
        now_ms - snapshot_.last_success_ms >= kNoSuccessTimeoutMs) {
        publish(ExternalPowerState::Unknown, now_ms);
    }
    return snapshot_;
}

const char* externalPowerStateName(ExternalPowerState value) noexcept {
    switch (value) {
        case ExternalPowerState::Present: return "PRESENT";
        case ExternalPowerState::Absent: return "ABSENT";
        case ExternalPowerState::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* workloadProfileName(WorkloadProfile value) noexcept {
    return value == WorkloadProfile::Home ? "home" : "go";
}

const char* profileSourceName(ProfileSource value) noexcept {
    switch (value) {
        case ProfileSource::AutoExternal: return "auto_external";
        case ProfileSource::AutoNoExternal: return "auto_no_external";
        case ProfileSource::AutoFailsafe: return "auto_failsafe";
        case ProfileSource::Manual: return "manual";
    }
    return "auto_failsafe";
}

}  // namespace edgefocus::power
