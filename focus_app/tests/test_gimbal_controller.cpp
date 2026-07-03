#include "device/GimbalController.h"
#include "app/Logger.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

void test_parse_stat_kv_payload() {
    using namespace edgefocus;
    GimbalSnapshot snap;
    std::string error;
    const std::string line = "STAT,SAFE=0,BOOST=2,MOTOR=0,HOLD=0,XHOME=1,YHOME=1,CAL=1,XT=5,YT=3,EN=1,IX=34,IY=-12,XLIM=180,YLIM=359";

    assert(GimbalController::parseStat(line, snap, &error));
    assert(error.empty());
    assert(snap.connected == true);
    assert(snap.online == true);
    assert(snap.safe == true);
    assert(snap.xhome == true);
    assert(snap.yhome == true);
    assert(snap.cal == true);
    assert(snap.boost == 2);
    assert(snap.motor == 0);
    assert(snap.en == 1);
    assert(snap.ix == 34);
    assert(snap.iy == -12);
    assert(snap.angle_x > 4.99f && snap.angle_x < 5.01f);
    assert(snap.angle_y > 2.99f && snap.angle_y < 3.01f);
}

void test_parse_stat_comma_payload() {
    using namespace edgefocus;
    GimbalSnapshot snap;
    std::string error;
    assert(GimbalController::parseStat("STAT:1,0,1,1,1,0,1,0,1.2,2.4", snap, &error));
    assert(error.empty());
    assert(snap.connected == true);
    assert(snap.online == true);
    assert(snap.safe == true);
    assert(snap.boost == 1);
    assert(snap.motor == 1);
}

void test_parse_angle_payload() {
    using namespace edgefocus;
    float x = 0.0f;
    float y = 0.0f;
    std::string error;
    assert(GimbalController::parseAngle("ANGLE:12.5,-4.8", x, y, &error));
    assert(error.empty());
    assert(x > 12.49f && x < 12.51f);
    assert(y > -4.81f && y < -4.79f);

    assert(GimbalController::parseAngle("ANGLE,X=123,Y=456,XD=13.3,YD=5.0,XHOME=120,YHOME=450,EN=1", x, y, &error));
    assert(error.empty());
    assert(x > 13.29f && x < 13.31f);
    assert(y > 4.99f && y < 5.01f);
}

void test_parse_error_response() {
    using namespace edgefocus;
    GimbalSnapshot snap;
    std::string error;
    assert(!GimbalController::parseStat("ERR,CONTROL_DISABLED", snap, &error));
    assert(!error.empty());
    assert(error.find("ERR") != std::string::npos);
}

void test_parse_fault_response_marks_axis_fault_unsafe() {
    using namespace edgefocus;
    GimbalSnapshot snap;
    std::string error;

    assert(!GimbalController::parseFault(
        "FAULT,XHOME_E=0,YHOME_E=0,XPOS_E=14,YPOS_E=0,IX=0,IY=0,LIMIT=X_BLOCKED,LRAW=2781",
        snap, &error));
    assert(!snap.safe);
    assert(!snap.online);
    assert(error.find("XPOS_E=14") != std::string::npos);
    assert(error.find("X_BLOCKED") != std::string::npos);

    assert(GimbalController::parseFault(
        "FAULT,XHOME_E=0,YHOME_E=0,XPOS_E=0,YPOS_E=0,IX=0,IY=0",
        snap, &error));
    assert(error.empty());
    assert(snap.connected);
    assert(snap.online);
}

void test_no_motion_commands() {
    using namespace edgefocus;
    assert(GimbalController::commandForState(GimbalController::PollState::OFFLINE) == "VER");
    assert(GimbalController::commandForState(GimbalController::PollState::VER) == "VER");
    assert(GimbalController::commandForState(GimbalController::PollState::PING) == "PING");
    assert(GimbalController::commandForState(GimbalController::PollState::STOP) == "STOP");
    assert(GimbalController::commandForState(GimbalController::PollState::STAT) == "STAT");
    assert(GimbalController::commandForState(GimbalController::PollState::FAULT) == "FAULT");
    assert(GimbalController::commandForState(GimbalController::PollState::ANGLE) == "ANGLE");
    assert(GimbalController::commandForState((GimbalController::PollState)99) == "VER");

    std::string cmd = GimbalController::commandForState(GimbalController::PollState::OFFLINE);
    assert(cmd.find("BOOST") == std::string::npos);
    assert(cmd.find("MOTOR") == std::string::npos);
    assert(cmd.find("TEST") == std::string::npos);
    assert(cmd.find("POS") == std::string::npos);
}

void test_auto_initialization_waits_for_full_fault_angle_cycle() {
    using namespace edgefocus;
    GimbalSnapshot snap;
    snap.online = true;
    snap.safe = true;

    assert(!GimbalController::readyForAutoInitialization(
        snap, GimbalController::PollState::FAULT));
    assert(!GimbalController::readyForAutoInitialization(
        snap, GimbalController::PollState::ANGLE));
    assert(GimbalController::readyForAutoInitialization(
        snap, GimbalController::PollState::STAT));

    snap.safe = false;
    assert(!GimbalController::readyForAutoInitialization(
        snap, GimbalController::PollState::STAT));
}

int main() {
    edgefocus::Logger::setLevel(edgefocus::LogLevel::ERROR);
    test_parse_stat_kv_payload();
    test_parse_stat_comma_payload();
    test_parse_angle_payload();
    test_parse_error_response();
    test_parse_fault_response_marks_axis_fault_unsafe();
    test_no_motion_commands();
    test_auto_initialization_waits_for_full_fault_angle_cycle();
    std::cout << "All GimbalController tests passed!\n";
    return 0;
}
