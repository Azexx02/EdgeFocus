#include "platform/power/Stusb4500Status.h"

#include <cassert>
#include <iostream>

using namespace edgefocus::power;

int main() {
    assert(decodeRawState(0x00, 0x00) == ExternalPowerState::Absent);
    assert(decodeRawState(0x01, 0x08) == ExternalPowerState::Present);
    assert(decodeRawState(0x01, 0x00) == ExternalPowerState::Unknown);
    assert(decodeRawState(0x00, 0x08) == ExternalPowerState::Unknown);

    SourceDebouncer debouncer;
    assert(debouncer.updateSuccess(0x00, 0x00, 0).state == ExternalPowerState::Unknown);
    assert(debouncer.updateSuccess(0x00, 0x00, 250).state == ExternalPowerState::Unknown);
    auto snapshot = debouncer.updateSuccess(0x00, 0x00, 500);
    assert(snapshot.state == ExternalPowerState::Absent);
    assert(snapshot.last_confirmed_state == ExternalPowerState::Absent);

    snapshot = debouncer.updateSuccess(0x01, 0x00, 750);
    assert(snapshot.state == ExternalPowerState::Absent);
    snapshot = debouncer.updateSuccess(0x01, 0x00, 2749);
    assert(snapshot.state == ExternalPowerState::Absent);
    snapshot = debouncer.updateSuccess(0x01, 0x00, 2750);
    assert(snapshot.state == ExternalPowerState::Unknown);
    assert(snapshot.last_confirmed_state == ExternalPowerState::Absent);

    debouncer.updateSuccess(0x01, 0x08, 3000);
    debouncer.updateSuccess(0x01, 0x08, 3250);
    snapshot = debouncer.updateSuccess(0x01, 0x08, 3500);
    assert(snapshot.state == ExternalPowerState::Present);

    debouncer.updateFailure(3750);
    snapshot = debouncer.updateFailure(4000);
    assert(snapshot.state == ExternalPowerState::Present);
    snapshot = debouncer.updateFailure(4250);
    assert(snapshot.state == ExternalPowerState::Unknown);
    assert(snapshot.last_confirmed_state == ExternalPowerState::Present);

    std::cout << "STUSB4500 status tests passed\n";
    return 0;
}
