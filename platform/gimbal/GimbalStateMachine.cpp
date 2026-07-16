#include "platform/gimbal/GimbalStateMachine.h"

#include <utility>

namespace edgefocus::gimbal {

void StateMachine::reset(std::int64_t now_ms) {
    status_ = Status{};
    status_.state_since_ms = now_ms;
}

bool StateMachine::transition(State expected, State next, std::int64_t now_ms) {
    if (status_.state != expected) return false;
    status_.state = next;
    status_.state_since_ms = now_ms;
    ++status_.generation;
    return true;
}

bool StateMachine::beginLinkCheck(std::int64_t now_ms) {
    if (status_.state != State::Disabled) return false;
    status_.recoverable = false;
    status_.link_healthy = false;
    return transition(State::Disabled, State::LinkCheck, now_ms);
}

bool StateMachine::markLinkHealthy(std::int64_t now_ms) {
    if (status_.state != State::LinkCheck) return false;
    status_.link_healthy = true;
    status_.last_event_ms = now_ms;
    return true;
}

bool StateMachine::beginAlignment(std::int64_t now_ms) {
    if (status_.state != State::LinkCheck || !status_.link_healthy) return false;
    return transition(State::LinkCheck, State::Aligning, now_ms);
}

bool StateMachine::beginMove(std::int64_t now_ms) {
    if (status_.state != State::Ready && status_.state != State::Holding) return false;
    const State current = status_.state;
    return transition(current, State::Moving, now_ms);
}

bool StateMachine::markHolding(std::int64_t now_ms) {
    if (status_.state != State::Moving && status_.state != State::Ready) return false;
    const State current = status_.state;
    return transition(current, State::Holding, now_ms);
}

bool StateMachine::beginStop(std::int64_t now_ms) {
    if (status_.state != State::Ready && status_.state != State::Moving &&
        status_.state != State::Holding) {
        return false;
    }
    const State current = status_.state;
    return transition(current, State::Stopping, now_ms);
}

bool StateMachine::markStopped(std::int64_t now_ms) {
    if (!transition(State::Stopping, State::Disabled, now_ms)) return false;
    status_.link_healthy = false;
    status_.fault_active = false;
    status_.recoverable = false;
    return true;
}

void StateMachine::updateEvent(const Event& event, std::int64_t now_ms) {
    status_.last_event_ms = now_ms;
    if (event.yaw_deg) status_.yaw_deg = event.yaw_deg;
    if (event.pitch_deg) status_.pitch_deg = event.pitch_deg;
    if (event.fault_active) status_.fault_active = *event.fault_active;
}

bool StateMachine::onEvent(const Event& event, std::int64_t now_ms) {
    updateEvent(event, now_ms);
    if (event.kind == EventKind::Error) {
        tripFault(now_ms, event.raw.empty() ? "g431_error" : event.raw);
        return false;
    }
    if (event.fault_active.value_or(false)) {
        tripFault(now_ms, "g431_fault_event");
        return false;
    }
    if (status_.state == State::Aligning && event.kind == EventKind::BootAlignOk) {
        status_.state = State::Ready;
        status_.state_since_ms = now_ms;
        status_.fault_active = false;
        ++status_.generation;
        return true;
    }
    if (status_.state == State::Stopping && event.kind == EventKind::Ok &&
        event.subject == "STOP") {
        return markStopped(now_ms);
    }
    return true;
}

bool StateMachine::onTimeout(std::int64_t now_ms, std::string reason) {
    if (status_.state == State::Disabled || status_.state == State::Fault) return false;
    tripFault(now_ms, std::move(reason));
    return true;
}

void StateMachine::tripFault(std::int64_t now_ms, std::string reason) {
    status_.state = State::Fault;
    status_.state_since_ms = now_ms;
    status_.fault_active = true;
    status_.recoverable = false;
    status_.link_healthy = false;
    status_.recovery_healthy_count = 0;
    status_.fault_latched_reason = std::move(reason);
    ++status_.generation;
}

bool StateMachine::recoveryProbeDue(std::int64_t now_ms) const noexcept {
    return status_.state == State::Fault &&
           (status_.last_recovery_probe_ms == 0 ||
            now_ms - status_.last_recovery_probe_ms >= kRecoveryIntervalMs);
}

bool StateMachine::beginRecoveryProbe(std::int64_t now_ms) {
    if (!recoveryProbeDue(now_ms)) return false;
    status_.state = State::RecoveryCheck;
    status_.state_since_ms = now_ms;
    status_.last_recovery_probe_ms = now_ms;
    ++status_.generation;
    return true;
}

bool StateMachine::finishRecoveryProbe(std::int64_t now_ms, bool healthy) {
    if (status_.state != State::RecoveryCheck) return false;
    if (!healthy) {
        status_.recovery_healthy_count = 0;
        status_.state = State::Fault;
        status_.state_since_ms = now_ms;
        ++status_.generation;
        return false;
    }

    ++status_.recovery_healthy_count;
    if (status_.recovery_healthy_count < kRecoveryHealthySamples) {
        status_.state = State::Fault;
        status_.state_since_ms = now_ms;
        ++status_.generation;
        return false;
    }

    status_.state = State::Disabled;
    status_.state_since_ms = now_ms;
    status_.fault_active = false;
    status_.recoverable = true;
    status_.link_healthy = false;
    ++status_.generation;
    return true;
}

}  // namespace edgefocus::gimbal
