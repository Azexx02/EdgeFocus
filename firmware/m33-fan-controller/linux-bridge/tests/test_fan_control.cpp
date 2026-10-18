#include "FanControlCore.h"
#include "PwmMapping.h"

#include <cstdlib>
#include <iostream>

using edgefocus::fan::ControlState;
using edgefocus::fan::CoolingMode;
using edgefocus::fan::FanConfig;
using edgefocus::fan::FanControlCore;
using edgefocus::fan::Fault;
using edgefocus::fan::hasFault;
using edgefocus::fan::logicalDutyToComplementaryCompareTicks;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void sample(FanControlCore& core,
            std::uint32_t sequence,
            std::int32_t temperature_mc,
            std::uint32_t time_ms) {
    require(core.acceptTemperature(sequence, temperature_mc, time_ms),
            "temperature sample should be accepted");
    core.update(time_ms);
}

void testBootRecoveryCurveAndHysteresis() {
    FanControlCore core;
    auto output = core.update(0);
    require(output.state == ControlState::BootSafe, "boot must be safe");
    require(output.duty_ns == 8000, "boot must use safe high speed");

    sample(core, 1, 44000, 100);
    require(core.output().state == ControlState::FailSafe,
            "one temperature sample must not clear safe state");
    sample(core, 2, 44000, 1100);
    require(core.output().state == ControlState::Controlled,
            "two valid samples must enable control");
    require(core.output().curve_band == 0 && core.output().duty_ns == 34000,
            "44C must select low curve band");

    sample(core, 3, 45000, 2100);
    sample(core, 4, 45000, 3100);
    require(core.output().curve_band == 1 && core.output().duty_ns == 30000,
            "filtered 45C must raise the curve band immediately");
    sample(core, 5, 44000, 4100);
    require(core.output().curve_band == 1,
            "falling one degree must remain inside 2C hysteresis");
    sample(core, 6, 42999, 5100);
    sample(core, 7, 42999, 6100);
    require(core.output().curve_band == 0,
            "falling below 43C must release the 45C band");
}

void testSequenceRangeStaleAndRecovery() {
    FanControlCore core;
    sample(core, 10, 50000, 0);
    sample(core, 11, 50000, 1000);
    require(!core.acceptTemperature(11, 51000, 2000),
            "duplicate sequence must be rejected");
    require(!core.acceptTemperature(9, 51000, 2500),
            "older sequence must be rejected");

    auto output = core.update(4000);
    require(output.state == ControlState::FailSafe, "stale input must fail safe");
    require(hasFault(output.current_faults, Fault::TempStale),
            "stale input must report TEMP_STALE");
    require(output.duty_ns == 8000, "stale input must use safe duty");

    sample(core, 12, 51000, 4100);
    require(core.output().state == ControlState::FailSafe,
            "one new frame must not clear stale state");
    sample(core, 13, 51000, 5100);
    require(core.output().state == ControlState::Controlled,
            "two new frames must recover stale state");

    require(!core.acceptTemperature(14, 151000, 5200),
            "out-of-range temperature must be rejected");
    output = core.update(5200);
    require(output.state == ControlState::FailSafe,
            "range error must immediately fail safe");
    require(hasFault(output.current_faults, Fault::TempRangeInvalid),
            "range error must be visible");
    require(hasFault(output.latched_faults, Fault::TempRangeInvalid),
            "range error must be latched");
}

void testAdaptiveRampHoldResumeAndExit() {
    FanControlCore core;
    std::uint32_t sequence = 1;
    sample(core, sequence++, 65000, 0);
    sample(core, sequence++, 65000, 1000);
    require(core.output().duty_ns == 14000, "65C curve duty must be 14000ns");

    for (std::uint32_t second = 2; second <= 5; ++second) {
        sample(core, sequence++, 70000, second * 1000);
    }
    require(core.output().mode == CoolingMode::BoostRamp,
            "three filtered samples at 70C must enter boost ramp");
    require(core.output().duty_ns == 14000,
            "boost must start from the current curve speed");

    for (std::uint32_t second = 6; second <= 10; ++second) {
        sample(core, sequence++, 70000, second * 1000);
    }
    require(core.output().duty_ns == 12000,
            "boost ramp must increase speed by 2000ns every five seconds");

    for (std::uint32_t second = 11; second <= 16; ++second) {
        const std::int32_t temperature =
            70000 - static_cast<std::int32_t>(second - 10) * 1000;
        sample(core, sequence++, temperature, second * 1000);
    }
    require(core.output().mode == CoolingMode::BoostHold,
            "a 0.5C-or-more ten-second decline must hold current speed");
    const std::uint32_t held_duty = core.output().duty_ns;

    for (std::uint32_t second = 17; second <= 26; ++second) {
        sample(core, sequence++, 65000, second * 1000);
    }
    sample(core, sequence++, 67000, 27000);
    sample(core, sequence++, 68000, 28000);
    sample(core, sequence++, 68000, 29000);
    require(core.output().mode == CoolingMode::BoostRamp,
            "two reheating trend windows must resume ramping");
    require(core.output().duty_ns == held_duty,
            "resume must not jump the PWM before the next ramp interval");

    // The median filter reaches <=62C after the transition. Ten stable seconds later the
    // controller returns to normal, retaining a rate-limited speed release.
    for (std::uint32_t second = 30; second <= 43; ++second) {
        sample(core, sequence++, 61000, second * 1000);
    }
    require(core.output().mode == CoolingMode::Normal,
            "ten seconds at or below 62C must exit adaptive boost");
    require(core.output().duty_ns <= 14000,
            "boost exit must not abruptly drop fan speed");
}

void testCriticalAndExternalFaults() {
    FanControlCore core;
    sample(core, 1, 79000, 0);
    sample(core, 2, 81000, 1000);
    sample(core, 3, 81000, 2000);
    require(core.output().mode == CoolingMode::BoostCritical,
            "filtered 80C must enter critical mode without waiting for ramp");
    require(core.output().duty_ns == 8000, "critical mode must use safe maximum speed");

    core.setExternalFault(Fault::RpmsgDisconnected, true);
    auto output = core.update(2100);
    require(output.state == ControlState::FailSafe,
            "RPMsg disconnect must force fail safe");
    core.setExternalFault(Fault::RpmsgDisconnected, false);
    sample(core, 4, 70000, 2200);
    sample(core, 5, 70000, 2300);
    require(core.output().state == ControlState::Controlled,
            "two valid samples after reconnect must restore control");
}

void testTachometerAndStall() {
    FanConfig config;
    config.tach_enabled = true;
    FanControlCore core(config);
    sample(core, 1, 65000, 0);
    sample(core, 2, 65000, 1000);
    require(core.acceptTachWindow(100, 1000, 1000), "tach window must be accepted");
    require(core.output().rpm == 3000, "100 pulses/s at 2 PPR must equal 3000 RPM");

    require(core.acceptTachWindow(0, 1000, 2000), "zero tach window 1");
    require(core.acceptTachWindow(0, 1000, 3000), "zero tach window 2");
    require(core.acceptTachWindow(0, 1000, 4000), "zero tach window 3");
    auto output = core.update(4000);
    require(hasFault(output.current_faults, Fault::FanStall),
            "three zero-RPM high-speed windows must report stall");
    require(output.state == ControlState::FailSafe && output.duty_ns == 8000,
            "stall must retain safe maximum PWM");
}

void testComplementaryPwmMapping() {
    std::uint32_t compare = 999;
    require(logicalDutyToComplementaryCompareTicks(8000, 40000, 1000, compare),
            "safe logical duty must map to CH1N compare");
    require(compare == 32, "8000ns logical duty must map to 32 ticks");

    require(logicalDutyToComplementaryCompareTicks(36000, 40000, 1000, compare),
            "low-speed logical duty must map to CH1N compare");
    require(compare == 4, "36000ns logical duty must map to 4 ticks");

    require(logicalDutyToComplementaryCompareTicks(0, 40000, 1000, compare),
            "zero logical duty is a valid endpoint");
    require(compare == 40, "zero logical duty must map to the period boundary");
    require(logicalDutyToComplementaryCompareTicks(40000, 40000, 1000, compare),
            "full logical duty is a valid endpoint");
    require(compare == 0, "full logical duty must map to zero compare");

    require(!logicalDutyToComplementaryCompareTicks(41000, 40000, 1000, compare),
            "out-of-range duty must be rejected");
    require(!logicalDutyToComplementaryCompareTicks(8500, 40000, 1000, compare),
            "sub-tick duty must be rejected");
    require(!logicalDutyToComplementaryCompareTicks(8000, 40500, 1000, compare),
            "sub-tick period must be rejected");
    require(!logicalDutyToComplementaryCompareTicks(8000, 40000, 0, compare),
            "zero tick must be rejected");
}

}  // namespace

int main() {
    testBootRecoveryCurveAndHysteresis();
    testSequenceRangeStaleAndRecovery();
    testAdaptiveRampHoldResumeAndExit();
    testCriticalAndExternalFaults();
    testTachometerAndStall();
    testComplementaryPwmMapping();
    std::cout << "fan control tests passed\n";
    return 0;
}
