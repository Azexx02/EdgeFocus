#include "platform/gimbal/GimbalProtocol.h"

#include <algorithm>
#include <cassert>
#include <iostream>

using namespace edgefocus::gimbal;

int main() {
    LineFramer framer(255);
    auto lines = framer.feed("VER,PROTO=3.0,");
    assert(lines.empty());
    lines = framer.feed("CAPS=POSE|HOLD_NOW\r\nOK,HELLO,PROTO=3.0\n");
    assert(lines.size() == 2);

    LineFramer short_framer(16);
    lines = short_framer.feed("0123456789abcdefX\nPING\n");
    assert(lines.size() == 1 && lines.front() == "PING");
    assert(short_framer.droppedLines() == 1);

    auto event = parseEvent(
        "VER,PROTO=3.0,FW=G431-GIMBAL,"
        "CAPS=ASYNC_MOTION|POSE|ORIGIN|FAULT_CLASS|HOLD_NOW|NVM_V3|NEGOTIATION_REQUIRED");
    assert(event.kind == EventKind::Version);
    assert(event.protocol_version == "3.0");
    assert(hasRequiredProtocol3Capabilities(event));
    assert(std::find(event.capabilities.begin(), event.capabilities.end(), "POSE") !=
           event.capabilities.end());

    event = parseEvent("OK,HELLO,PROTO=3.0");
    assert(event.kind == EventKind::HelloAck);
    event = parseEvent("OK,ARM,STATE=READY,CAL_EPOCH=7,ORIGIN_EPOCH=9");
    assert(event.kind == EventKind::ArmAck);
    assert(event.device_state == State::Ready);
    assert(event.calibration_epoch == 7 && event.origin_epoch == 9);
    event = parseEvent("OK,HOLD_NOW,GEN=41,YAW10=3590,PITCH10=20");
    assert(event.kind == EventKind::HoldAck);
    assert(event.motion_generation == 41);
    assert(event.yaw_deg && *event.yaw_deg == 359.0);

    event = parseEvent("TRACK,ACCEPTED,GEN=42,YAW10=100,PITCH10=3550");
    assert(event.kind == EventKind::MotionAccepted);
    assert(event.motion_generation == 42);
    assert(event.yaw_deg && *event.yaw_deg == 10.0);
    assert(event.pitch_deg && *event.pitch_deg == 355.0);

    event = parseEvent(
        "POSE,STATE=MOVING,ARM_PROFILE=PRODUCTION,P_RAW=1,Y_RAW=2,"
        "YAW10=100,PITCH10=3550,TYAW10=120,TPITCH10=30,GEN=42,SETTLED=0,"
        "CAL_EPOCH=7,ORIGIN_EPOCH=9,CAL_VALID=1,ORIGIN_VALID=1,"
        "CURRENT_PROTECTION=VALID,SAMPLE_AGE_MS=4,FAULT=NONE,OP=NONE,"
        "OP_GEN=0,OP_PROGRESS=0,LAST_OP_STATUS=OK");
    assert(event.kind == EventKind::Pose);
    assert(event.device_state == State::Moving);
    assert(event.arm_profile == ArmProfile::Production);
    assert(event.motion_generation == 42 && event.settled == false);
    assert(event.target_yaw_deg && *event.target_yaw_deg == 12.0);
    assert(event.target_pitch_deg && *event.target_pitch_deg == 3.0);
    assert(event.calibration_valid == true && event.origin_valid == true);
    assert(event.current_protection_state == "VALID");
    assert(event.sample_age_ms == 4);

    event = parseEvent("ERR,CLASS=COMMAND,CODE=INVALID_STATE");
    assert(event.kind == EventKind::Error);
    assert(event.error_class == ErrorClass::Command);
    assert(event.error_code == "INVALID_STATE");

    event = parseEvent("FAULT,STATE=READY,ACTIVE=0,UARTERR=7,RXDROP=1,TXDROP=2");
    assert(isHealthyFaultEvent(event));
    assert(event.uart_errors == 7 && event.rx_drops == 1 && event.tx_drops == 2);

    // Protocol 2 remains parseable for read-only diagnosis, but cannot satisfy
    // the protocol 3 capability gate.
    event = parseEvent("VER,PROTO=2.0,CAPS=UART_ONLY|ARM|TRACK");
    assert(event.kind == EventKind::Version);
    assert(!hasRequiredProtocol3Capabilities(event));
    event = parseEvent("ANGLE,P_DEG=313,Y_DEG=9");
    assert(event.kind == EventKind::Angle);
    assert(event.yaw_deg == 9.0 && event.pitch_deg == 313.0);

    assert(diagnosticCommand(EventKind::Ping) == "PING");
    assert(diagnosticCommand(EventKind::Pose) == "POSE");
    assert(helloCommand() == "HELLO HOST PROTO=3.0");
    assert(stopCommand() == "STOP");
    assert(holdCommand() == "HOLD NOW");
    assert(armCommand() == "ARM");
    assert(armCommand(true) == "ARM MAINTENANCE");
    assert(moveCommand(12.0, -4.5) == "TRACK YP 120 3555");
    assert(trackCommand(-1.0, 4.5) == "TRACK YP 3590 45");
    assert(normalizeAngle(-1.0) == 359.0);
    assert(shortestAngleDelta(359.0, 2.0) == 3.0);
    assert(shortestAngleDelta(2.0, 359.0) == -3.0);
    assert(offsetAngle(355.0, 10.0) == 5.0);

    std::cout << "gimbal protocol 3 tests passed\n";
    return 0;
}
