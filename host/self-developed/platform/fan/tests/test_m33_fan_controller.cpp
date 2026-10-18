#include "FanControllerApp.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace edgefocus::fan;
using namespace edgefocus::fan::m33;
using namespace edgefocus::fan::protocol;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct FakePlatform {
    std::vector<std::uint32_t> duties;
    std::vector<FrameBuffer> frames;
    bool pwm_ok{true};
    bool rpmsg_ok{true};
};

bool writePwm(void* context, std::uint32_t duty) {
    auto& platform = *static_cast<FakePlatform*>(context);
    platform.duties.push_back(duty);
    return platform.pwm_ok;
}

bool sendFrame(void* context, const std::uint8_t* data, std::size_t size) {
    auto& platform = *static_cast<FakePlatform*>(context);
    if (!platform.rpmsg_ok || size > kMaxFrameSize) {
        return false;
    }
    FrameBuffer frame;
    frame.size = size;
    std::copy(data, data + size, frame.bytes.begin());
    platform.frames.push_back(frame);
    return true;
}

FrameBuffer bridgeHello(std::uint32_t sequence,
                        std::uint32_t now,
                        std::uint64_t boot_id = 123) {
    FrameBuffer frame;
    require(encodeHello(sequence, now, {CapabilityLinuxBridge, boot_id}, frame),
            "bridge HELLO encode");
    return frame;
}

FrameBuffer temperature(std::uint32_t sequence,
                        std::uint32_t now,
                        std::int32_t value) {
    FrameBuffer frame;
    require(encodeTempSample(sequence, now, {value, 1}, frame), "temperature encode");
    return frame;
}

FrameBuffer shutdownNotice(std::uint32_t sequence, std::uint32_t now) {
    FrameBuffer frame;
    require(encodeShutdownNotice(sequence, now, {0}, frame), "shutdown encode");
    return frame;
}

void testSafeBootHandshakeControlAndTimeout() {
    FakePlatform platform;
    PlatformCallbacks callbacks{&platform, writePwm, sendFrame};
    FanControllerApp app(callbacks, {0xABCDEFu, 1000, 5000, false});

    require(app.initialize(0), "safe PWM initialization must succeed");
    require(platform.duties.size() == 1 && platform.duties.back() == 8000,
            "safe duty must be written before transport setup");
    app.onTransportReady(10);
    require(!platform.frames.empty(), "transport ready must advertise M33 HELLO");
    DecodedFrame decoded;
    require(decodeFrame(platform.frames.back().bytes.data(), platform.frames.back().size,
                        decoded) == DecodeError::None,
            "M33 HELLO must decode");
    HelloPayload hello;
    require(decodeHello(decoded, hello) &&
                (hello.capabilities & CapabilityM33Controller) != 0,
            "HELLO must identify the M33 controller role");

    const FrameBuffer bridge = bridgeHello(1, 20);
    require(app.onRpmsgFrame(bridge.bytes.data(), bridge.size, 20),
            "bridge HELLO must complete handshake");
    require(app.protocolReady(), "protocol must become ready");

    FrameBuffer sample = temperature(2, 100, 44000);
    require(app.onRpmsgFrame(sample.bytes.data(), sample.size, 100), "sample 1");
    app.tick(100);
    sample = temperature(3, 1100, 44000);
    require(app.onRpmsgFrame(sample.bytes.data(), sample.size, 1100), "sample 2");
    app.tick(1100);
    require(app.output().state == ControlState::Controlled &&
                platform.duties.back() == 34000,
            "two temperatures must enter normal curve control");

    // A new bridge process restarts its sequence counter. A changed boot_id
    // must open a new sequence domain instead of rejecting all future frames.
    const FrameBuffer restarted_bridge = bridgeHello(1, 1200, 456);
    require(app.onRpmsgFrame(restarted_bridge.bytes.data(), restarted_bridge.size, 1200),
            "new bridge boot id must be accepted");
    require(app.output().state == ControlState::BootSafe,
            "new bridge boot id must return temperature input to boot safe");
    sample = temperature(2, 1300, 44000);
    require(app.onRpmsgFrame(sample.bytes.data(), sample.size, 1300),
            "new sequence sample 1");
    app.tick(1300);
    sample = temperature(3, 2300, 44000);
    require(app.onRpmsgFrame(sample.bytes.data(), sample.size, 2300),
            "new sequence sample 2");
    app.tick(2300);
    require(app.output().state == ControlState::Controlled,
            "new sequence domain must recover after two samples");

    // systemd stops the Linux bridge cleanly and it sends ShutdownNotice.
    // A later bridge process reuses the still-live rpmsg-tty endpoint, so its
    // HELLO must restore both transport and protocol state without rebooting M33.
    const FrameBuffer shutdown = shutdownNotice(4, 2400);
    require(app.onRpmsgFrame(shutdown.bytes.data(), shutdown.size, 2400),
            "shutdown notice must be accepted");
    require(!app.protocolReady(), "shutdown notice must leave protocol disconnected");
    const FrameBuffer reattached_bridge = bridgeHello(1, 2500, 789);
    require(app.onRpmsgFrame(reattached_bridge.bytes.data(), reattached_bridge.size, 2500),
            "bridge HELLO must reattach after a clean shutdown");
    require(app.protocolReady(), "reattached bridge must restore protocol readiness");
    sample = temperature(2, 2600, 44000);
    require(app.onRpmsgFrame(sample.bytes.data(), sample.size, 2600),
            "reattached sequence sample 1");
    app.tick(2600);
    sample = temperature(3, 3600, 44000);
    require(app.onRpmsgFrame(sample.bytes.data(), sample.size, 3600),
            "reattached sequence sample 2");
    app.tick(3600);
    require(app.output().state == ControlState::Controlled,
            "reattached bridge must recover control after two samples");

    app.tick(7000);
    require(app.output().state == ControlState::FailSafe &&
                platform.duties.back() == 8000,
            "stale temperature must restore safe high speed");
}

void testProtocolAndPwmFaults() {
    FakePlatform platform;
    FanControllerApp app({&platform, writePwm, sendFrame}, {7, 1000, 5000, false});
    require(app.initialize(0), "initialize");

    FrameBuffer hello = bridgeHello(1, 1);
    hello.bytes[4] = kProtocolMajor + 1;
    require(!app.onRpmsgFrame(hello.bytes.data(), hello.size, 1),
            "incompatible major must be rejected");
    app.tick(1);
    require(hasFault(app.output().current_faults, Fault::ProtocolIncompatible),
            "major mismatch must force an explicit protocol fault");

    app.onTransportReady(10);
    hello = bridgeHello(2, 20);
    require(app.onRpmsgFrame(hello.bytes.data(), hello.size, 20), "compatible HELLO");
    FrameBuffer sample = temperature(3, 100, 44000);
    require(app.onRpmsgFrame(sample.bytes.data(), sample.size, 100), "sample 1");
    app.tick(100);
    sample = temperature(4, 1100, 44000);
    require(app.onRpmsgFrame(sample.bytes.data(), sample.size, 1100), "sample 2");
    platform.pwm_ok = false;
    app.tick(1100);
    require(!app.healthyForWatchdog(), "PWM write failure must suppress watchdog health");
}

}  // namespace

int main() {
    testSafeBootHandshakeControlAndTimeout();
    testProtocolAndPwmFaults();
    std::cout << "M33 fan controller tests passed\n";
    return 0;
}
