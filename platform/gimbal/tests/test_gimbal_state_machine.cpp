#include "platform/gimbal/GimbalStateMachine.h"

#include <cassert>
#include <iostream>

using namespace edgefocus::gimbal;

int main() {
    StateMachine machine;
    assert(machine.status().state == State::Disabled);
    assert(!machine.beginMove(1));
    assert(machine.beginLinkCheck(10));
    assert(!machine.beginAlignment(11));
    assert(machine.markLinkHealthy(12));
    assert(machine.beginAlignment(13));
    assert(machine.status().state == State::Aligning);

    Event start;
    start.kind = EventKind::BootAlignStart;
    assert(machine.onEvent(start, 20));
    assert(machine.status().state == State::Aligning);
    Event ok;
    ok.kind = EventKind::BootAlignOk;
    assert(machine.onEvent(ok, 30));
    assert(machine.status().state == State::Ready);

    assert(machine.beginMove(40));
    assert(machine.markHolding(50));
    assert(machine.beginStop(60));
    Event stop;
    stop.kind = EventKind::Ok;
    stop.subject = "STOP";
    assert(machine.onEvent(stop, 70));
    assert(machine.status().state == State::Disabled);

    assert(machine.beginLinkCheck(80));
    assert(machine.markLinkHealthy(81));
    assert(machine.beginAlignment(82));
    assert(machine.onTimeout(3083, "align_timeout"));
    assert(machine.status().state == State::Fault);
    assert(machine.status().fault_latched_reason == "align_timeout");
    assert(machine.recoveryProbeDue(3083));
    assert(machine.beginRecoveryProbe(3083));
    assert(!machine.finishRecoveryProbe(3090, true));
    assert(machine.status().state == State::Fault);
    assert(!machine.recoveryProbeDue(8082));
    assert(machine.recoveryProbeDue(8083));
    assert(machine.beginRecoveryProbe(8083));
    assert(!machine.finishRecoveryProbe(8090, true));
    assert(machine.beginRecoveryProbe(13083));
    assert(machine.finishRecoveryProbe(13090, true));
    assert(machine.status().state == State::Disabled);
    assert(machine.status().recoverable);
    assert(machine.status().fault_latched_reason == "align_timeout");
    assert(!machine.beginMove(13091));
    assert(machine.beginLinkCheck(13092));

    machine.tripFault(14000, "pd6_low");
    assert(machine.beginRecoveryProbe(19000));
    assert(!machine.finishRecoveryProbe(19001, false));
    assert(machine.status().recovery_healthy_count == 0);
    assert(machine.status().state == State::Fault);

    std::cout << "gimbal state machine tests passed\n";
    return 0;
}
