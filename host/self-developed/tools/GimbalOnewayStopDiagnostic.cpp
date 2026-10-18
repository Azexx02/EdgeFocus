#include "tools/GimbalOnewayStopDiagnostic.h"

#include <algorithm>
#include <chrono>

namespace edgefocus::gimbal::diagnostic {
namespace {

class SafeClose final {
public:
    explicit SafeClose(IGimbalIo& io) : io_(io) {}
    ~SafeClose() {
        std::string ignored;
        (void)io_.setEnable(false, ignored);
        io_.close();
    }

    SafeClose(const SafeClose&) = delete;
    SafeClose& operator=(const SafeClose&) = delete;

private:
    IGimbalIo& io_;
};

}  // namespace

GimbalOnewayStopDiagnostic::GimbalOnewayStopDiagnostic(
    IGimbalIo& io, OnewayStopTiming timing)
    : io_(io), timing_(timing) {}

bool GimbalOnewayStopDiagnostic::observe(int duration_ms,
                                         OnewayStopResult& result) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(0, duration_ms));
    do {
        bool fault_active = false;
        if (!io_.readFaultActive(fault_active, result.error)) return false;
        if (fault_active) {
            result.fault_active = true;
            result.error = "G431_FAULT_OUT active";
            return false;
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) return true;
        const int timeout_ms = static_cast<int>(
            std::min<long long>(std::max(1, timing_.poll_ms), remaining));
        std::string bytes;
        if (!io_.readBytes(bytes, timeout_ms, result.error)) return false;
        result.uart_transcript += bytes;
    } while (Clock::now() < deadline);
    return true;
}

bool GimbalOnewayStopDiagnostic::send(const char* command,
                                      OnewayStopResult& result) {
    if (!io_.writeLine(command, result.error)) return false;
    result.commands_sent.emplace_back(command);
    return true;
}

OnewayStopResult GimbalOnewayStopDiagnostic::run() {
    return runSequence(false);
}

OnewayStopResult GimbalOnewayStopDiagnostic::runPitchFive() {
    return runSequence(true);
}

OnewayStopResult GimbalOnewayStopDiagnostic::runSequence(bool include_pitch_five) {
    OnewayStopResult result;
    if (!io_.open(result.error)) return result;
    SafeClose safe_close(io_);

    if (!io_.setBoot0(false, result.error) ||
        !io_.setEnable(false, result.error) ||
        !observe(timing_.safe_low_ms, result)) {
        return result;
    }
    if (!send("PING", result)) return result;

    if (!io_.setEnable(true, result.error) ||
        !observe(timing_.alignment_observation_ms, result)) {
        return result;
    }
    if (!send("HOLD 1", result) ||
        !observe(timing_.hold_observation_ms, result)) {
        return result;
    }
    if (include_pitch_five &&
        (!send("PITCH 5", result) ||
         !observe(timing_.pitch_observation_ms, result))) {
        return result;
    }
    if (!send("STOP", result) ||
        !observe(timing_.stop_observation_ms, result)) {
        return result;
    }

    result.sequence_completed = true;
    return result;
}

}  // namespace edgefocus::gimbal::diagnostic
