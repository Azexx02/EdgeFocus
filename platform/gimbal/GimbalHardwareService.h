#pragma once

#include "app/Module.h"
#include "platform/gimbal/GimbalProtocol.h"
#include "platform/gimbal/GimbalStateMachine.h"
#include "platform/gimbal/IGimbalIo.h"
#include "platform/gimbal/IGimbalSessionHardware.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace edgefocus::gimbal {

class GimbalHardwareService final : public Module, public IGimbalSessionHardware {
public:
    struct MotionLimits {
        std::optional<double> yaw_min_offset_deg;
        std::optional<double> yaw_max_offset_deg;
        std::optional<double> pitch_min_offset_deg;
        std::optional<double> pitch_max_offset_deg;

        bool verified() const noexcept {
            return yaw_min_offset_deg && yaw_max_offset_deg &&
                   pitch_min_offset_deg && pitch_max_offset_deg &&
                   *yaw_min_offset_deg < 0.0 && *yaw_max_offset_deg > 0.0 &&
                   *pitch_min_offset_deg < 0.0 && *pitch_max_offset_deg > 0.0 &&
                   *yaw_min_offset_deg < *yaw_max_offset_deg &&
                   *pitch_min_offset_deg < *pitch_max_offset_deg &&
                   *yaw_min_offset_deg >= -180.0 && *yaw_max_offset_deg <= 180.0 &&
                   *pitch_min_offset_deg >= -180.0 && *pitch_max_offset_deg <= 180.0;
        }
        bool contains(double reference_yaw_deg, double reference_pitch_deg,
                      double yaw_deg, double pitch_deg) const noexcept {
            const double yaw_offset = shortestAngleDelta(reference_yaw_deg, yaw_deg);
            const double pitch_offset = shortestAngleDelta(reference_pitch_deg, pitch_deg);
            return verified() && yaw_offset >= *yaw_min_offset_deg &&
                   yaw_offset <= *yaw_max_offset_deg &&
                   pitch_offset >= *pitch_min_offset_deg &&
                   pitch_offset <= *pitch_max_offset_deg;
        }
    };

    using Clock = std::function<std::int64_t()>;
    using Sleeper = std::function<void(std::chrono::milliseconds)>;

    explicit GimbalHardwareService(
        std::unique_ptr<IGimbalIo> io,
        MotionLimits limits = {},
        Clock clock = {},
        Sleeper sleeper = {});
    ~GimbalHardwareService() override;

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    bool start(std::string* error = nullptr);
    void stop() noexcept;
    void pollOnce();

    Status status() const override;
    CommandResult probeDiagnostics(int timeout_ms_per_command = 500);
    CommandResult enableAndAlign(int timeout_ms = 10000) override;
    CommandResult stopAndDisable(int timeout_ms = 1000) override;
    CommandResult moveTo(double yaw_deg, double pitch_deg, int timeout_ms = 2000) override;
    CommandResult moveToVerified(double yaw_deg, double pitch_deg,
                                 int timeout_ms = 8000,
                                 double tolerance_deg = 1.5) override;
    CommandResult moveToManualVerified(double yaw_deg, double pitch_deg,
                                       int timeout_ms = 8000,
                                       double tolerance_deg = 1.5) override;
    CommandResult rebaseAlignmentAtCurrent(int timeout_ms = 1000) override;
    CommandResult trackTo(double yaw_deg, double pitch_deg, int timeout_ms = 500) override;
    CommandResult hold(int timeout_ms = 1000) override;
    CommandResult refreshAngle(int timeout_ms = 500) override;
    CommandResult freezeAtCurrent(int timeout_ms = 2000) override;
    CommandResult probeRecovery();

private:
    using Matcher = std::function<bool(const Event&)>;

    CommandResult waitFor(const Matcher& matcher, int timeout_ms, Event* matched = nullptr,
                          bool enforce_fault_pin = true);
    CommandResult runDiagnosticsLocked(int timeout_ms_per_command,
                                       bool continue_after_failure,
                                       bool enforce_fault_pin);
    CommandResult moveToInternal(double yaw_deg, double pitch_deg, int timeout_ms,
                                 bool enforce_limits);
    CommandResult moveToVerifiedInternal(double yaw_deg, double pitch_deg,
                                         int timeout_ms, double tolerance_deg,
                                         bool enforce_limits);
    void dispatchBytes(const std::string& bytes, const Matcher& matcher, Event* matched,
                       bool& matched_result, Event* device_error,
                       std::vector<std::string>* transcript);
    void tripFaultLocked(const std::string& reason);
    void emergencyStopLocked() noexcept;
    bool setSafeLinesLocked(std::string& error);
    static std::int64_t defaultClockMs();

    std::unique_ptr<IGimbalIo> io_;
    MotionLimits limits_;
    Clock clock_;
    Sleeper sleeper_;
    mutable std::recursive_mutex operation_mutex_;
    StateMachine machine_;
    LineFramer framer_;
    bool initialized_{};
    std::int64_t next_start_attempt_ms_{};
    std::optional<double> alignment_yaw_deg_;
    std::optional<double> alignment_pitch_deg_;
    std::int64_t next_health_probe_ms_{};
    unsigned consecutive_health_failures_{};
    std::optional<unsigned> last_uart_errors_;
};

}  // namespace edgefocus::gimbal
