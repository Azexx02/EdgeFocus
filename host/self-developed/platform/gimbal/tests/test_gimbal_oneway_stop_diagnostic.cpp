#include "tools/GimbalOnewayStopDiagnostic.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

using edgefocus::gimbal::IGimbalIo;
using edgefocus::gimbal::diagnostic::GimbalOnewayStopDiagnostic;
using edgefocus::gimbal::diagnostic::OnewayStopTiming;

class FakeIo final : public IGimbalIo {
public:
    bool open(std::string& error) override {
        events.emplace_back("open");
        open_ = open_ok;
        if (!open_ok) error = "open failed";
        return open_ok;
    }
    void close() noexcept override {
        events.emplace_back("close");
        open_ = false;
    }
    bool isOpen() const noexcept override { return open_; }
    bool setBoot0(bool high, std::string&) override {
        events.emplace_back(std::string("boot0:") + (high ? "1" : "0"));
        return true;
    }
    bool setEnable(bool high, std::string&) override {
        events.emplace_back(std::string("enable:") + (high ? "1" : "0"));
        return true;
    }
    bool readFaultActive(bool& active, std::string&) override {
        ++fault_reads;
        active = fault_on_read > 0 && fault_reads >= fault_on_read;
        return true;
    }
    bool writeLine(const std::string& line, std::string& error) override {
        events.emplace_back("write:" + line);
        if (line == fail_write) {
            error = "write failed";
            return false;
        }
        writes.push_back(line);
        return true;
    }
    bool readBytes(std::string& bytes, int, std::string&) override {
        bytes.clear();
        return true;
    }

    bool open_ok{true};
    int fault_on_read{0};
    int fault_reads{0};
    std::string fail_write;
    std::vector<std::string> events;
    std::vector<std::string> writes;

private:
    bool open_{false};
};

OnewayStopTiming fastTiming() {
    OnewayStopTiming timing;
    timing.safe_low_ms = 0;
    timing.alignment_observation_ms = 0;
    timing.hold_observation_ms = 0;
    timing.pitch_observation_ms = 0;
    timing.stop_observation_ms = 0;
    timing.poll_ms = 1;
    return timing;
}

void assertEndsSafe(const FakeIo& io) {
    assert(io.events.size() >= 2);
    assert(io.events[io.events.size() - 2] == "enable:0");
    assert(io.events.back() == "close");
}

void testExactWhitelistAndSafeExit() {
    FakeIo io;
    GimbalOnewayStopDiagnostic diagnostic(io, fastTiming());
    const auto result = diagnostic.run();
    assert(result.sequence_completed);
    assert((io.writes == std::vector<std::string>{"PING", "HOLD 1", "STOP"}));
    assert(std::find(io.events.begin(), io.events.end(), "enable:1") != io.events.end());
    assertEndsSafe(io);
}

void testFaultPreventsMotionSequence() {
    FakeIo io;
    io.fault_on_read = 1;
    GimbalOnewayStopDiagnostic diagnostic(io, fastTiming());
    const auto result = diagnostic.run();
    assert(!result.sequence_completed);
    assert(result.fault_active);
    assert(io.writes.empty());
    assert(std::find(io.events.begin(), io.events.end(), "enable:1") == io.events.end());
    assertEndsSafe(io);
}

void testPitchFiveHasExactWhitelistAndSafeExit() {
    FakeIo io;
    GimbalOnewayStopDiagnostic diagnostic(io, fastTiming());
    const auto result = diagnostic.runPitchFive();
    assert(result.sequence_completed);
    assert((io.writes ==
            std::vector<std::string>{"PING", "HOLD 1", "PITCH 5", "STOP"}));
    assertEndsSafe(io);
}

void testWriteFailureStillDisables() {
    FakeIo io;
    io.fail_write = "HOLD 1";
    GimbalOnewayStopDiagnostic diagnostic(io, fastTiming());
    const auto result = diagnostic.run();
    assert(!result.sequence_completed);
    assert((io.writes == std::vector<std::string>{"PING"}));
    assertEndsSafe(io);
}

}  // namespace

int main() {
    testExactWhitelistAndSafeExit();
    testFaultPreventsMotionSequence();
    testPitchFiveHasExactWhitelistAndSafeExit();
    testWriteFailureStillDisables();
    std::cout << "gimbal one-way STOP diagnostic tests passed\n";
    return 0;
}
