#pragma once

#include "platform/power/PowerTypes.h"

#include <cstdint>
#include <optional>

namespace edgefocus::power {

ExternalPowerState decodeRawState(std::uint8_t port_status,
                                  std::uint8_t monitoring_status) noexcept;
RawStatus decodeRawStatus(std::uint8_t port_status,
                          std::uint8_t monitoring_status) noexcept;

class SourceDebouncer {
public:
    static constexpr unsigned kConfirmSamples = 3;
    static constexpr unsigned kFailureSamples = 3;
    static constexpr std::int64_t kUnknownTimeoutMs = 2000;
    static constexpr std::int64_t kNoSuccessTimeoutMs = 2000;

    SourceSnapshot updateSuccess(std::uint8_t port_status, std::uint8_t monitoring_status,
                                 std::int64_t now_ms);
    SourceSnapshot updateFailure(std::int64_t now_ms);
    SourceSnapshot snapshot() const noexcept { return snapshot_; }

private:
    void publish(ExternalPowerState state, std::int64_t now_ms);

    SourceSnapshot snapshot_;
    bool has_success_{};
    std::optional<ExternalPowerState> candidate_;
    unsigned candidate_count_{};
    std::optional<std::int64_t> raw_unknown_since_ms_;
};

}  // namespace edgefocus::power
