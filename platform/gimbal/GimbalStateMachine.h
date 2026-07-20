#pragma once

#include "platform/gimbal/GimbalTypes.h"

#include <cstdint>
#include <string>

namespace edgefocus::gimbal {

class StateMachine {
public:
    static constexpr std::int64_t kAlignmentTimeoutMs = 10000;
    static constexpr std::int64_t kRecoveryIntervalMs = 5000;
    static constexpr unsigned kRecoveryHealthySamples = 3;

    const Status& status() const noexcept { return status_; }

    void reset(std::int64_t now_ms);

    bool beginLinkCheck(std::int64_t now_ms);
    bool markLinkHealthy(std::int64_t now_ms);
    bool beginAlignment(std::int64_t now_ms);
    bool markReady(std::int64_t now_ms);
    bool beginMove(std::int64_t now_ms);
    bool markHolding(std::int64_t now_ms);
    bool beginStop(std::int64_t now_ms);
    bool markStopped(std::int64_t now_ms);
    bool onEvent(const Event& event, std::int64_t now_ms);
    bool onTimeout(std::int64_t now_ms, std::string reason);
    void tripFault(std::int64_t now_ms, std::string reason);
    bool recoveryProbeDue(std::int64_t now_ms) const noexcept;
    bool beginRecoveryProbe(std::int64_t now_ms);
    bool finishRecoveryProbe(std::int64_t now_ms, bool healthy);

private:
    bool transition(State expected, State next, std::int64_t now_ms);
    void updateEvent(const Event& event, std::int64_t now_ms);

    Status status_;
};

}  // namespace edgefocus::gimbal
