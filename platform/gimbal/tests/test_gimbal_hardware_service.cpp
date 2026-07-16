#include "platform/gimbal/GimbalHardwareService.h"
#include "app/AppContext.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace edgefocus::gimbal;
using edgefocus::AppContext;

namespace {

class FakeIo final : public IGimbalIo {
public:
    bool open(std::string& error) override {
        opened = open_ok;
        error = open_ok ? "" : "open_failed";
        return open_ok;
    }
    void close() noexcept override { opened = false; }
    bool isOpen() const noexcept override { return opened; }
    ControlMode controlMode() const noexcept override { return mode; }
    bool setBoot0(bool high, std::string& error) override {
        if (high) {
            error = "boot0_high_forbidden";
            return false;
        }
        boot0 = false;
        return true;
    }
    bool setEnable(bool high, std::string& /*error*/) override {
        enable = high;
        enable_history.push_back(high);
        operations.push_back(high ? "enable:1" : "enable:0");
        return set_enable_ok;
    }
    bool readFaultActive(bool& active, std::string& error) override {
        active = fault_active;
        error.clear();
        return fault_read_ok;
    }
    bool writeLine(const std::string& line, std::string& error) override {
        writes.push_back(line);
        operations.push_back("write:" + line);
        error.clear();
        return write_ok;
    }
    bool readBytes(std::string& bytes, int /*timeout_ms*/, std::string& error) override {
        error.clear();
        bytes.clear();
        if (!read_ok) {
            error = "read_failed";
            return false;
        }
        if (!reads.empty()) {
            bytes = reads.front();
            reads.pop_front();
        }
        return true;
    }

    bool opened{};
    bool open_ok{true};
    bool set_enable_ok{true};
    bool fault_read_ok{true};
    bool write_ok{true};
    bool read_ok{true};
    bool boot0{};
    bool enable{};
    bool fault_active{};
    ControlMode mode{ControlMode::UartGpioInterlock};
    std::vector<bool> enable_history;
    std::vector<std::string> writes;
    std::vector<std::string> operations;
    std::deque<std::string> reads;
};

}  // namespace

int main() {
    {
        std::int64_t uart_now = 1000;
        auto uart_fake = std::make_unique<FakeIo>();
        auto* uart_io = uart_fake.get();
        uart_io->mode = ControlMode::UartOnly;
        GimbalHardwareService::MotionLimits limits;
        limits.yaw_min_offset_deg = -30;
        limits.yaw_max_offset_deg = 30;
        limits.pitch_min_offset_deg = -30;
        limits.pitch_max_offset_deg = 30;
        GimbalHardwareService uart_service(
            std::move(uart_fake), limits, [&] { uart_now += 10; return uart_now; },
            [&](std::chrono::milliseconds delay) { uart_now += delay.count(); });
        std::string uart_error;
        assert(uart_service.start(&uart_error));
        uart_io->reads.push_back("OK,STOP\n");
        uart_io->reads.push_back(
            "STAT,MODE=UART_ONLY,BOOST=0,MOTOR=0,HOLD=0,FAULT=0\n");
        assert(uart_service.stopAndDisable().success);
        assert(uart_io->writes[0] == "STOP" && uart_io->writes[1] == "STAT");
        uart_io->reads.push_back("OK,G431\n");
        uart_io->reads.push_back(
            "VER,PROTO=2.0,FW=G431-GIMBAL,CAPS=UART_ONLY|ARM|YP_CONTINUOUS|FOC_HOLD|TRACK|CIRCLE\n");
        uart_io->reads.push_back("BOOT_ALIGN,START\n");
        uart_io->reads.push_back("BOOT_ALIGN,OK\nOK,ARM\n");
        uart_io->reads.push_back("VER,PROTO=2.0,CAPS=UART_ONLY|ARM|TRACK\n");
        uart_io->reads.push_back("OK,G431\n");
        uart_io->reads.push_back("STAT,MODE=UART_ONLY,FAULT=0\n");
        uart_io->reads.push_back("FAULT,ACTIVE=0,UARTERR=1,RXDROP=0,TXDROP=0\n");
        uart_io->reads.push_back("ANGLE,P_DEG=20,Y_DEG=10\n");
        auto uart_result = uart_service.enableAndAlign();
        assert(uart_result.success);
        assert(std::find(uart_io->writes.begin(), uart_io->writes.end(), "ARM") !=
               uart_io->writes.end());
        assert(std::find(uart_io->enable_history.begin(), uart_io->enable_history.end(), true) ==
               uart_io->enable_history.end());

        uart_io->reads.push_back("SET,YP_OK,Y=12,P=22\n");
        uart_io->reads.push_back("ANGLE,P_DEG=22,Y_DEG=12\n");
        assert(uart_service.moveToVerified(12, 22, 2000, 1.5).success);
        assert(uart_io->writes[uart_io->writes.size() - 2] == "SET YP 12 22");
        assert(uart_io->writes.back() == "ANGLE");

        uart_io->reads.push_back("ANGLE,P_DEG=22,Y_DEG=12\n");
        uart_io->reads.push_back("SET,YP_OK,Y=12,P=22\n");
        uart_io->reads.push_back("ANGLE,P_DEG=22,Y_DEG=12\n");
        uart_io->reads.push_back("ANGLE,P_DEG=22,Y_DEG=12\n");
        assert(uart_service.freezeAtCurrent(2000).success);
        assert(uart_service.status().state == State::Holding);

        uart_io->reads.push_back("SET,YP_OK,Y=12,P=22\n");
        assert(uart_service.moveTo(12, 22).success);
        assert(uart_io->writes.back() == "SET YP 12 22");
        uart_io->reads.push_back("TRACK,YP_OK,Y10=125,P10=215\n");
        assert(uart_service.trackTo(12.5, 21.5).success);
        assert(uart_io->writes.back() == "TRACK YP 125 215");
        uart_io->reads.push_back("OK,HOLD_ON,P=22,Y=12\n");
        assert(uart_service.hold().success);
        uart_io->reads.push_back("OK,STOP\n");
        uart_io->reads.push_back(
            "STAT,MODE=UART_ONLY,BOOST=0,MOTOR=0,HOLD=0,FAULT=0\n");
        assert(uart_service.stopAndDisable().success);
        uart_service.stop();
    }

    std::int64_t now = 1000;
    auto fake = std::make_unique<FakeIo>();
    auto* io = fake.get();
    GimbalHardwareService service(
        std::move(fake), {}, [&] { now += 10; return now; },
        [&](std::chrono::milliseconds delay) { now += delay.count(); });

    std::string error;
    assert(service.start(&error));
    assert(!io->boot0 && !io->enable);

    io->fault_active = true;
    io->reads.push_back("VER,1.0\n");
    io->reads.push_back("PONG\n");
    io->reads.push_back("STAT,FAULT=1,EN=0,BOOT0=0\n");
    io->reads.push_back("FAULT,ACTIVE=1\n");
    io->reads.push_back("ANGLE,0,0\n");
    auto diagnostic = service.probeDiagnostics();
    assert(diagnostic.success);
    assert(diagnostic.failed_steps.empty());
    assert(diagnostic.transcript.size() == 5);
    assert(!io->enable);
    assert(service.status().state == State::Fault);
    service.stop();
    io->fault_active = false;
    assert(service.start(&error));
    io->operations.clear();
    io->writes.clear();

    io->reads.push_back("OK,G431\n");
    io->reads.push_back("BOOT_ALIGN,START\n");
    io->reads.push_back("BOOT_ALIGN,OK\n");
    io->reads.push_back("VER,1.0\n");
    io->reads.push_back("PONG\n");
    io->reads.push_back("STAT,FAULT=0,EN=1,BOOT0=0\n");
    io->reads.push_back("FAULT,ACTIVE=0\n");
    io->reads.push_back("ANGLE,YAW=0,PITCH=0\n");
    auto result = service.enableAndAlign();
    assert(result.success);
    assert(service.status().state == State::Ready);
    assert(io->enable);
    const auto enable_it = std::find(io->operations.begin(), io->operations.end(), "enable:1");
    const auto ping_it = std::find(io->operations.begin(), io->operations.end(), "write:PING");
    assert(enable_it != io->operations.end() && ping_it != io->operations.end());
    assert(ping_it < enable_it);

    result = service.moveTo(0, 0);
    assert(!result.success && result.error == "motion_limits_unverified");
    assert(io->writes.back() != moveCommand(0, 0));

    io->reads.push_back("OK,STOP\n");
    result = service.stopAndDisable();
    assert(result.success);
    assert(service.status().state == State::Disabled);
    assert(!io->enable);

    io->reads.push_back("OK,G431\n");
    io->reads.push_back("BOOT_ALIGN,OK\n");
    io->reads.push_back("VER,1.0\n");
    io->reads.push_back("PONG\n");
    io->reads.push_back("STAT,FAULT=0,EN=1,BOOT0=0\n");
    io->reads.push_back("FAULT,ACTIVE=0\n");
    io->reads.push_back("ANGLE,YAW=0,PITCH=0\n");
    assert(service.enableAndAlign().success);
    io->fault_active = true;
    service.pollOnce();
    assert(service.status().state == State::Fault);
    assert(!io->enable);

    io->fault_active = false;
    for (int probe = 0; probe < 3; ++probe) {
        now += StateMachine::kRecoveryIntervalMs;
        io->reads.push_back("PONG\n");
        io->reads.push_back("FAULT,ACTIVE=0\n");
        io->reads.push_back("STAT,FAULT=0,EN=0,BOOT0=0\n");
        result = service.probeRecovery();
        assert(result.success);
        assert(!io->enable);
    }
    assert(service.status().state == State::Disabled);
    assert(service.status().recoverable);

    for (const auto& command : io->writes) {
        assert(command.rfind("SET ", 0) != 0);
        assert(command.rfind("TRACK ", 0) != 0);
    }

    service.stop();
    assert(!io->opened);
    assert(!io->enable);

    auto no_ping_fake = std::make_unique<FakeIo>();
    auto* no_ping_io = no_ping_fake.get();
    std::int64_t no_ping_now = 100;
    GimbalHardwareService no_ping_service(
        std::move(no_ping_fake), {}, [&] { no_ping_now += 10; return no_ping_now; },
        [&](std::chrono::milliseconds delay) { no_ping_now += delay.count(); });
    assert(no_ping_service.start(&error));
    const auto no_ping_result = no_ping_service.enableAndAlign();
    assert(!no_ping_result.success);
    assert(no_ping_service.status().state == State::Fault);
    assert(std::count(no_ping_io->writes.begin(), no_ping_io->writes.end(), "PING") == 3);
    assert(std::find(no_ping_io->enable_history.begin(), no_ping_io->enable_history.end(), true) ==
           no_ping_io->enable_history.end());
    no_ping_service.stop();

    auto retry_fake = std::make_unique<FakeIo>();
    auto* retry_io = retry_fake.get();
    retry_io->open_ok = false;
    std::int64_t retry_now = 100;
    GimbalHardwareService retry_service(
        std::move(retry_fake), {}, [&] { return retry_now; },
        [&](std::chrono::milliseconds delay) { retry_now += delay.count(); });
    AppContext app_context;
    assert(retry_service.init(app_context));
    assert(!retry_io->opened);
    retry_io->open_ok = true;
    retry_now += StateMachine::kRecoveryIntervalMs;
    retry_service.runOnce(app_context);
    assert(retry_io->opened);
    retry_service.shutdown(app_context);

    std::cout << "gimbal hardware service tests passed\n";
    return 0;
}
