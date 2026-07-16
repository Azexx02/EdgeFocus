#include "platform/gimbal/GimbalProtocol.h"

#include <cassert>
#include <iostream>

using namespace edgefocus::gimbal;

int main() {
    LineFramer framer(16);
    auto lines = framer.feed("BOOT_ALIGN,");
    assert(lines.empty());
    lines = framer.feed("START\r\nOK,STOP\n");
    assert(lines.size() == 2);
    assert(lines[0] == "BOOT_ALIGN,START");
    assert(lines[1] == "OK,STOP");

    lines = framer.feed("0123456789abcdefX\nPING\n");
    assert(lines.size() == 1 && lines.front() == "PING");
    assert(framer.droppedLines() == 1);

    auto event = parseEvent("BOOT_ALIGN,START");
    assert(event.kind == EventKind::BootAlignStart);
    event = parseEvent("BOOT_ALIGN,OK");
    assert(event.kind == EventKind::BootAlignOk);
    event = parseEvent("OK,STOP");
    assert(event.kind == EventKind::Ok && event.subject == "STOP");
    event = parseEvent("OK,G431");
    assert(event.kind == EventKind::Ok && event.subject == "G431");
    assert(isHealthyPingEvent(event));
    event = parseEvent("FAULT,ACTIVE=0,ENABLE_IN=1,BOOT0=0");
    assert(event.kind == EventKind::FaultStatus);
    assert(event.fault_active.has_value() && !*event.fault_active);
    assert(event.enable_in.has_value() && *event.enable_in);
    assert(event.boot0.has_value() && !*event.boot0);
    assert(isHealthyFaultEvent(event));

    event = parseEvent("STAT,EN=1,BOOT0=0,FAULT=0,YAW=12.5,PITCH=-3.25");
    assert(event.kind == EventKind::Status);
    assert(isHealthyStatusEvent(event));
    assert(event.yaw_deg && *event.yaw_deg == 12.5);
    assert(event.pitch_deg && *event.pitch_deg == -3.25);

    event = parseEvent("ANGLE,90.5,20");
    assert(event.kind == EventKind::Angle);
    assert(event.yaw_deg && *event.yaw_deg == 90.5);
    assert(event.pitch_deg && *event.pitch_deg == 20.0);

    event = parseEvent("ANGLE,P_RAW=313,Y_RAW=9,P_DEG=313,Y_DEG=9,P_HOME=0,Y_HOME=0,EN=0");
    assert(event.kind == EventKind::Angle);
    assert(event.yaw_deg && *event.yaw_deg == 9.0);
    assert(event.pitch_deg && *event.pitch_deg == 313.0);
    event = parseEvent("FAULT,ACTIVE=0,UARTERR=7,RXDROP=0,TXDROP=0");
    assert(event.uart_errors && *event.uart_errors == 7);
    assert(event.rx_drops && *event.rx_drops == 0);
    assert(event.tx_drops && *event.tx_drops == 0);
    assert(parseEvent("SET,YP_OK,Y=10,P=8").kind == EventKind::SetAck);
    assert(parseEvent("TRACK,YP_OK,Y10=120,P10=80").kind == EventKind::TrackAck);

    assert(diagnosticCommand(EventKind::Ping) == "PING");
    assert(stopCommand() == "STOP");
    assert(holdCommand() == "HOLD 1");
    assert(armCommand() == "ARM");
    assert(moveCommand(12.0, 4.5) == "SET YP 12 5");
    assert(trackCommand(12.0, 4.5) == "TRACK YP 120 45");
    assert(normalizeAngle(-1.0) == 359.0);
    assert(shortestAngleDelta(359.0, 2.0) == 3.0);
    assert(shortestAngleDelta(2.0, 359.0) == -3.0);
    assert(offsetAngle(355.0, 10.0) == 5.0);

    std::cout << "gimbal protocol tests passed\n";
    return 0;
}
