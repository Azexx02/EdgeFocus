#include "platform/gimbal/GimbalHardwareService.h"

#include "app/AppContext.h"
#include "app/Logger.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

namespace edgefocus::gimbal {

std::int64_t GimbalHardwareService::defaultClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

GimbalHardwareService::GimbalHardwareService(
    std::unique_ptr<IGimbalIo> io, MotionLimits limits, Clock clock, Sleeper sleeper)
    : Module("GimbalHardwareService"),
      io_(std::move(io)),
      limits_(std::move(limits)),
      clock_(clock ? std::move(clock) : Clock(defaultClockMs)),
      sleeper_(sleeper ? std::move(sleeper)
                       : Sleeper([](std::chrono::milliseconds delay) {
                             std::this_thread::sleep_for(delay);
                         })) {}

GimbalHardwareService::~GimbalHardwareService() { stop(); }

bool GimbalHardwareService::init(AppContext& /*ctx*/) {
    std::string error;
    if (!start(&error)) {
        next_start_attempt_ms_ = clock_() + StateMachine::kRecoveryIntervalMs;
        LOGW("GimbalHardwareService",
             "Initialization degraded; fixed-camera mode remains available: %s",
             error.c_str());
    }
    // Gimbal is an optional process capability and must never prevent the
    // camera/Fusion/BLE core from starting.
    return true;
}

void GimbalHardwareService::runOnce(AppContext& /*ctx*/) {
    bool initialized = false;
    {
        std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
        initialized = initialized_;
    }
    if (!initialized) {
        const auto now_ms = clock_();
        if (now_ms >= next_start_attempt_ms_) {
            std::string error;
            if (start(&error)) {
                LOGI("GimbalHardwareService", "Gimbal I/O recovered");
                next_start_attempt_ms_ = 0;
            } else {
                next_start_attempt_ms_ = now_ms + StateMachine::kRecoveryIntervalMs;
                LOGW("GimbalHardwareService", "Gimbal I/O still unavailable: %s",
                     error.c_str());
            }
        }
        sleeper_(std::chrono::milliseconds(25));
        return;
    }
    pollOnce();
}
void GimbalHardwareService::shutdown(AppContext& /*ctx*/) { stop(); }

bool GimbalHardwareService::setSafeLinesLocked(std::string& error) {
    if (!io_->setBoot0(false, error)) return false;
    return io_->setEnable(false, error);
}

bool GimbalHardwareService::start(std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    if (initialized_) return true;
    std::string local_error;
    if (!io_ || !io_->open(local_error) || !setSafeLinesLocked(local_error)) {
        if (io_) io_->close();
        if (error) *error = local_error.empty() ? "gimbal I/O open failed" : local_error;
        return false;
    }
    initialized_ = true;
    next_start_attempt_ms_ = 0;
    machine_.reset(clock_());
    framer_.reset();
    alignment_yaw_deg_.reset();
    alignment_pitch_deg_.reset();
    next_health_probe_ms_ = 0;
    consecutive_health_failures_ = 0;
    last_uart_errors_.reset();
    if (error) error->clear();
    return true;
}

void GimbalHardwareService::stop() noexcept {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    if (!io_) return;
    if (initialized_) {
        const auto state = machine_.status().state;
        if (state == State::Ready || state == State::Moving || state == State::Holding) {
            (void)stopAndDisable(300);
        }
        std::string ignored;
        (void)io_->setEnable(false, ignored);
    }
    io_->close();
    initialized_ = false;
}

Status GimbalHardwareService::status() const {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    return machine_.status();
}

void GimbalHardwareService::dispatchBytes(const std::string& bytes, const Matcher& matcher,
                                          Event* matched, bool& matched_result,
                                          Event* device_error,
                                          std::vector<std::string>* transcript) {
    for (const auto& line : framer_.feed(bytes)) {
        const auto event = parseEvent(line);
        if (transcript) transcript->push_back(event.raw);
        (void)machine_.onEvent(event, clock_());
        if (device_error && device_error->raw.empty() && event.kind == EventKind::Error) {
            *device_error = event;
        }
        if (!matched_result && matcher && matcher(event)) {
            matched_result = true;
            if (matched) *matched = event;
        }
    }
}

CommandResult GimbalHardwareService::waitFor(const Matcher& matcher, int timeout_ms, Event* matched,
                                             bool enforce_fault_pin) {
    CommandResult result;
    const auto deadline = clock_() + std::max(1, timeout_ms);
    while (clock_() <= deadline) {
        bool fault_active = false;
        std::string error;
        if (!io_->readFaultActive(fault_active, error)) {
            result.error = error.empty() ? "failed to read PD6" : error;
            return result;
        }
        if (fault_active && enforce_fault_pin) {
            tripFaultLocked("pd6_low");
            result.error = "pd6_low";
            return result;
        }

        std::string bytes;
        if (!io_->readBytes(bytes, 25, error)) {
            result.error = error.empty() ? "UART read failed" : error;
            return result;
        }
        bool found = false;
        Event device_error;
        dispatchBytes(bytes, matcher, matched, found, &device_error, &result.transcript);
        if (!device_error.raw.empty()) {
            result.error = "device_error:" + device_error.raw;
            return result;
        }
        if (enforce_fault_pin && machine_.status().state == State::Fault) {
            result.error = machine_.status().fault_latched_reason;
            return result;
        }
        if (found) {
            result.success = true;
            return result;
        }
        if (bytes.empty()) sleeper_(std::chrono::milliseconds(1));
    }
    result.timeout = true;
    result.error = "timeout";
    return result;
}

CommandResult GimbalHardwareService::probeDiagnostics(int timeout_ms_per_command) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    CommandResult result;
    if (!initialized_) {
        result.error = "not_initialized";
        return result;
    }
    std::string error;
    if (!setSafeLinesLocked(error)) {
        result.error = error;
        return result;
    }

    sleeper_(std::chrono::milliseconds(500));
    return runDiagnosticsLocked(timeout_ms_per_command, true, false);
}

CommandResult GimbalHardwareService::runDiagnosticsLocked(int timeout_ms_per_command,
                                                           bool continue_after_failure,
                                                           bool enforce_fault_pin) {
    const struct Step {
        EventKind command;
        Matcher matcher;
    } steps[] = {
        {EventKind::Version, [](const Event& event) { return event.kind == EventKind::Version; }},
        {EventKind::Ping, isHealthyPingEvent},
        {EventKind::Status, [](const Event& event) { return event.kind == EventKind::Status; }},
        {EventKind::FaultStatus,
         [](const Event& event) { return event.kind == EventKind::FaultStatus; }},
        {EventKind::Angle, [](const Event& event) { return event.kind == EventKind::Angle; }},
    };
    CommandResult aggregate;
    for (const auto& step : steps) {
        const std::string command = diagnosticCommand(step.command);
        std::string error;
        if (!io_->writeLine(diagnosticCommand(step.command), error)) {
            aggregate.failed_steps.push_back(command);
            if (aggregate.error.empty()) aggregate.error = error;
            if (!continue_after_failure) break;
            continue;
        }
        auto result = waitFor(step.matcher, timeout_ms_per_command, nullptr,
                              enforce_fault_pin);
        aggregate.transcript.insert(aggregate.transcript.end(), result.transcript.begin(),
                                    result.transcript.end());
        if (!result.success) {
            aggregate.failed_steps.push_back(command);
            aggregate.timeout = aggregate.timeout || result.timeout;
            if (aggregate.error.empty()) aggregate.error = result.error;
            if (!continue_after_failure) break;
        }
    }
    aggregate.success = aggregate.failed_steps.empty();
    return aggregate;
}

void GimbalHardwareService::tripFaultLocked(const std::string& reason) {
    emergencyStopLocked();
    machine_.tripFault(clock_(), reason);
}

void GimbalHardwareService::emergencyStopLocked() noexcept {
    std::string ignored;
    if (io_->controlMode() == ControlMode::UartOnly) {
        (void)io_->writeEmergencyStop(ignored);
    }
    (void)io_->setEnable(false, ignored);
}

CommandResult GimbalHardwareService::enableAndAlign(int timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    CommandResult result;
    if (!initialized_) {
        result.error = "not_initialized";
        return result;
    }
    if (!machine_.beginLinkCheck(clock_())) {
        result.error = "invalid_state:" + std::string(stateName(machine_.status().state));
        return result;
    }

    std::string error;
    if (!setSafeLinesLocked(error)) {
        tripFaultLocked(error.empty() ? "safe_lines_failed" : error);
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    bool fault_active = true;
    if (!io_->readFaultActive(fault_active, error) || fault_active) {
        tripFaultLocked(fault_active ? "pd6_low" :
                                   (error.empty() ? "pd6_read_failed" : error));
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    // The G431 contract requires a disabled-state PING after power and UART
    // have settled. Never use an Enable edge as a link probe.
    sleeper_(std::chrono::milliseconds(500));
    CommandResult link_result;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!io_->writeLine(diagnosticCommand(EventKind::Ping), error)) {
            link_result.error = error.empty() ? "link_check_write_failed" : error;
            break;
        }
        auto attempt_result = waitFor(isHealthyPingEvent, 500);
        link_result.transcript.insert(link_result.transcript.end(),
                                      attempt_result.transcript.begin(),
                                      attempt_result.transcript.end());
        if (attempt_result.success) {
            link_result.success = true;
            break;
        }
        link_result.timeout = link_result.timeout || attempt_result.timeout;
        link_result.error = attempt_result.error;
        if (!attempt_result.timeout || attempt == 2) break;
        sleeper_(std::chrono::milliseconds(100));
    }
    if (!link_result.success) {
        tripFaultLocked(link_result.error.empty() ? "link_check_failed"
                                                  : link_result.error);
        link_result.error = machine_.status().fault_latched_reason;
        return link_result;
    }
    (void)machine_.markLinkHealthy(clock_());
    if (io_->controlMode() == ControlMode::UartOnly) {
        if (!io_->writeLine(diagnosticCommand(EventKind::Version), error)) {
            tripFaultLocked(error.empty() ? "version_write_failed" : error);
            result.error = machine_.status().fault_latched_reason;
            return result;
        }
        Event version;
        auto version_result = waitFor([](const Event& event) {
            const auto proto = event.fields.find("PROTO");
            const auto caps = event.fields.find("CAPS");
            return event.kind == EventKind::Version && proto != event.fields.end() &&
                   proto->second == "2.0" && caps != event.fields.end() &&
                   caps->second.find("UART_ONLY") != std::string::npos &&
                   caps->second.find("ARM") != std::string::npos &&
                   caps->second.find("TRACK") != std::string::npos;
        }, 500, &version);
        link_result.transcript.insert(link_result.transcript.end(),
                                      version_result.transcript.begin(),
                                      version_result.transcript.end());
        if (!version_result.success) {
            tripFaultLocked(version_result.timeout ? "protocol2_capability_timeout"
                                                   : "protocol2_capability_mismatch");
            version_result.error = machine_.status().fault_latched_reason;
            return version_result;
        }
    }

    sleeper_(std::chrono::milliseconds(100));
    if (!machine_.beginAlignment(clock_())) {
        tripFaultLocked("align_state_failed");
        result.success = false;
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    if (io_->controlMode() == ControlMode::UartOnly) {
        if (!io_->writeLine(armCommand(), error)) {
            tripFaultLocked(error.empty() ? "arm_write_failed" : error);
            result.error = machine_.status().fault_latched_reason;
            return result;
        }
        bool aligned = false;
        bool armed = false;
        result = waitFor([&](const Event& event) {
            if (event.kind == EventKind::BootAlignOk) aligned = true;
            if (event.kind == EventKind::Ok && event.subject == "ARM") armed = true;
            return aligned && armed;
        }, std::min(timeout_ms, static_cast<int>(StateMachine::kAlignmentTimeoutMs)));
    } else {
        if (!io_->setEnable(true, error)) {
            tripFaultLocked(error.empty() ? "enable_failed" : error);
            result.error = machine_.status().fault_latched_reason;
            return result;
        }
        result = waitFor([](const Event& event) {
            return event.kind == EventKind::BootAlignOk;
        }, std::min(timeout_ms, static_cast<int>(StateMachine::kAlignmentTimeoutMs)));
    }
    result.transcript.insert(result.transcript.begin(), link_result.transcript.begin(),
                             link_result.transcript.end());
    if (!result.success) {
        tripFaultLocked(result.timeout ? "align_timeout" : result.error);
        return result;
    }

    auto diagnostics = runDiagnosticsLocked(500, false, true);
    result.transcript.insert(result.transcript.end(), diagnostics.transcript.begin(),
                             diagnostics.transcript.end());
    result.failed_steps = diagnostics.failed_steps;
    result.timeout = diagnostics.timeout;
    if (!diagnostics.success) {
        tripFaultLocked("post_align_diagnostics:" +
                        (diagnostics.error.empty() ? std::string("failed")
                                                   : diagnostics.error));
        result.success = false;
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    const auto aligned_status = machine_.status();
    if (!aligned_status.yaw_deg || !aligned_status.pitch_deg) {
        tripFaultLocked("alignment_reference_unknown");
        result.success = false;
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    alignment_yaw_deg_ = normalizeAngle(*aligned_status.yaw_deg);
    alignment_pitch_deg_ = normalizeAngle(*aligned_status.pitch_deg);
    result.success = true;
    result.error.clear();
    return result;
}

CommandResult GimbalHardwareService::stopAndDisable(int timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    CommandResult result;
    if (!initialized_) {
        result.error = "not_initialized";
        return result;
    }
    const auto state = machine_.status().state;
    std::string error;
    if (state != State::Ready && state != State::Moving && state != State::Holding) {
        if (io_->controlMode() == ControlMode::UartOnly) {
            if (!io_->writeLine(stopCommand(), error)) {
                result.error = error.empty() ? "stop_write_failed" : error;
                return result;
            }
            result = waitFor([](const Event& event) {
                return event.kind == EventKind::Ok && event.subject == "STOP";
            }, timeout_ms, nullptr, false);
            if (!result.success) return result;
            if (!io_->writeLine(diagnosticCommand(EventKind::Status), error)) {
                result.success = false;
                result.error = error.empty() ? "stop_status_write_failed" : error;
                return result;
            }
            auto verified = waitFor([](const Event& event) {
                if (event.kind != EventKind::Status) return false;
                const auto is_zero = [&](const char* key) {
                    const auto it = event.fields.find(key);
                    return it != event.fields.end() && it->second == "0";
                };
                return is_zero("BOOST") && is_zero("MOTOR") && is_zero("HOLD") &&
                       !event.fault_active.value_or(true);
            }, timeout_ms, nullptr, false);
            result.transcript.insert(result.transcript.end(), verified.transcript.begin(),
                                     verified.transcript.end());
            if (!verified.success) {
                result.success = false;
                result.timeout = verified.timeout;
                result.error = verified.timeout ? "stop_state_verify_timeout"
                                                : "stop_state_verify_failed";
            }
            return result;
        }
        result.success = io_->setEnable(false, error);
        result.error = error;
        return result;
    }
    (void)machine_.beginStop(clock_());
    if (!io_->writeLine(stopCommand(), error)) {
        tripFaultLocked(error.empty() ? "stop_write_failed" : error);
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    result = waitFor([](const Event& event) {
        return event.kind == EventKind::Ok && event.subject == "STOP";
    }, timeout_ms);
    if (result.success && io_->controlMode() == ControlMode::UartOnly) {
        if (!io_->writeLine(diagnosticCommand(EventKind::Status), error)) {
            result.success = false;
            result.error = error.empty() ? "stop_status_write_failed" : error;
        } else {
            auto verified = waitFor([](const Event& event) {
                if (event.kind != EventKind::Status) return false;
                const auto is_zero = [&](const char* key) {
                    const auto it = event.fields.find(key);
                    return it != event.fields.end() && it->second == "0";
                };
                return is_zero("BOOST") && is_zero("MOTOR") && is_zero("HOLD") &&
                       !event.fault_active.value_or(true);
            }, timeout_ms, nullptr, false);
            result.transcript.insert(result.transcript.end(), verified.transcript.begin(),
                                     verified.transcript.end());
            if (!verified.success) {
                result.success = false;
                result.timeout = verified.timeout;
                result.error = verified.timeout ? "stop_state_verify_timeout"
                                                : "stop_state_verify_failed";
            }
        }
    }
    const bool disabled = io_->setEnable(false, error);
    if (!result.success || !disabled) {
        tripFaultLocked(!disabled ? error : (result.timeout ? "stop_timeout" : result.error));
        result.success = false;
        result.error = machine_.status().fault_latched_reason;
    }
    return result;
}

CommandResult GimbalHardwareService::moveTo(double yaw_deg, double pitch_deg, int timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    return moveToInternal(yaw_deg, pitch_deg, timeout_ms, true);
}

CommandResult GimbalHardwareService::moveToInternal(
    double yaw_deg, double pitch_deg, int timeout_ms, bool enforce_limits) {
    CommandResult result;
    if (enforce_limits && !limits_.verified()) {
        result.error = "motion_limits_unverified";
        return result;
    }
    if (!alignment_yaw_deg_ || !alignment_pitch_deg_) {
        result.error = "alignment_reference_unknown";
        return result;
    }
    if (enforce_limits &&
        !limits_.contains(*alignment_yaw_deg_, *alignment_pitch_deg_, yaw_deg, pitch_deg)) {
        result.error = "target_out_of_bounds";
        return result;
    }
    if (!machine_.beginMove(clock_())) {
        result.error = "invalid_state:" + std::string(stateName(machine_.status().state));
        return result;
    }
    std::string error;
    if (!io_->writeLine(moveCommand(yaw_deg, pitch_deg), error)) {
        tripFaultLocked(error.empty() ? "move_write_failed" : error);
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    result = waitFor([](const Event& event) {
        return event.kind == EventKind::SetAck;
    }, timeout_ms);
    if (result.success) {
        (void)machine_.markHolding(clock_());
    } else {
        tripFaultLocked(result.timeout ? "move_timeout" : result.error);
    }
    return result;
}

CommandResult GimbalHardwareService::refreshAngle(int timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    CommandResult aggregate;
    if (!initialized_) {
        aggregate.error = "not_initialized";
        return aggregate;
    }
    const auto state = machine_.status().state;
    if (state != State::Ready && state != State::Moving &&
        state != State::Holding) {
        aggregate.error = "invalid_state:" + std::string(stateName(state));
        return aggregate;
    }
    const int bounded_timeout = std::max(50, timeout_ms);
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string error;
        if (!io_->writeLine(diagnosticCommand(EventKind::Angle), error)) {
            aggregate.error = error.empty() ? "angle_write_failed" : error;
            break;
        }
        auto current = waitFor([](const Event& event) {
            return event.kind == EventKind::Angle &&
                   event.yaw_deg.has_value() && event.pitch_deg.has_value();
        }, bounded_timeout);
        aggregate.transcript.insert(aggregate.transcript.end(),
                                    current.transcript.begin(),
                                    current.transcript.end());
        if (current.success) {
            aggregate.success = true;
            aggregate.timeout = false;
            aggregate.error.clear();
            return aggregate;
        }
        aggregate.timeout = aggregate.timeout || current.timeout;
        aggregate.error = current.error.empty() ? "angle_query_failed"
                                                : current.error;
        if (attempt == 0) sleeper_(std::chrono::milliseconds(80));
    }
    tripFaultLocked(aggregate.error.empty() ? "angle_query_failed"
                                            : aggregate.error);
    aggregate.success = false;
    aggregate.error = machine_.status().fault_latched_reason;
    return aggregate;
}

CommandResult GimbalHardwareService::moveToVerified(
    double yaw_deg, double pitch_deg, int timeout_ms,
    double tolerance_deg) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    return moveToVerifiedInternal(
        yaw_deg, pitch_deg, timeout_ms, tolerance_deg, true);
}

CommandResult GimbalHardwareService::moveToManualVerified(
    double yaw_deg, double pitch_deg, int timeout_ms,
    double tolerance_deg) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    return moveToVerifiedInternal(
        yaw_deg, pitch_deg, timeout_ms, tolerance_deg, false);
}

CommandResult GimbalHardwareService::moveToVerifiedInternal(
    double yaw_deg, double pitch_deg, int timeout_ms,
    double tolerance_deg, bool enforce_limits) {
    CommandResult aggregate;
    if (timeout_ms <= 0 || tolerance_deg <= 0.0 || tolerance_deg > 5.0) {
        aggregate.error = "invalid_move_verify_parameters";
        return aggregate;
    }
    double command_yaw = yaw_deg;
    double command_pitch = pitch_deg;
    auto command = moveToInternal(
        command_yaw, command_pitch, std::min(timeout_ms, 2000), enforce_limits);
    aggregate.transcript.insert(aggregate.transcript.end(),
                                command.transcript.begin(),
                                command.transcript.end());
    if (!command.success) return command;

    const auto deadline_ms = clock_() + timeout_ms;
    std::optional<double> previous_yaw;
    std::optional<double> previous_pitch;
    unsigned compensation_count = 0;
    unsigned samples = 0;
    while (clock_() < deadline_ms && samples++ < 100) {
        sleeper_(std::chrono::milliseconds(100));
        auto angle = refreshAngle(500);
        aggregate.transcript.insert(aggregate.transcript.end(),
                                    angle.transcript.begin(),
                                    angle.transcript.end());
        if (!angle.success) {
            aggregate.success = false;
            aggregate.timeout = angle.timeout;
            aggregate.error = angle.error;
            return aggregate;
        }
        const auto current = machine_.status();
        if (!current.yaw_deg || !current.pitch_deg) {
            tripFaultLocked("move_verify_angle_unknown");
            aggregate.error = machine_.status().fault_latched_reason;
            return aggregate;
        }
        const double error = std::max(
            std::abs(shortestAngleDelta(*current.yaw_deg, yaw_deg)),
            std::abs(shortestAngleDelta(*current.pitch_deg, pitch_deg)));
        if (error <= tolerance_deg) {
            aggregate.success = true;
            aggregate.error.clear();
            return aggregate;
        }
        if (previous_yaw && previous_pitch) {
            const double stability = std::max(
                std::abs(shortestAngleDelta(*previous_yaw, *current.yaw_deg)),
                std::abs(shortestAngleDelta(*previous_pitch, *current.pitch_deg)));
            if (stability <= 0.5 && compensation_count < 2) {
                command_yaw = offsetAngle(
                    command_yaw,
                    shortestAngleDelta(*current.yaw_deg, yaw_deg));
                command_pitch = offsetAngle(
                    command_pitch,
                    shortestAngleDelta(*current.pitch_deg, pitch_deg));
                command = moveToInternal(
                    command_yaw, command_pitch,
                    std::min(timeout_ms, 2000), enforce_limits);
                aggregate.transcript.insert(aggregate.transcript.end(),
                                            command.transcript.begin(),
                                            command.transcript.end());
                if (!command.success) return command;
                ++compensation_count;
                previous_yaw.reset();
                previous_pitch.reset();
                continue;
            }
        }
        previous_yaw = *current.yaw_deg;
        previous_pitch = *current.pitch_deg;
    }
    tripFaultLocked("move_verify_timeout");
    aggregate.success = false;
    aggregate.timeout = true;
    aggregate.error = machine_.status().fault_latched_reason;
    return aggregate;
}

CommandResult GimbalHardwareService::rebaseAlignmentAtCurrent(int timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    auto result = refreshAngle(timeout_ms);
    if (!result.success) return result;
    const auto current = machine_.status();
    if (current.fault_active || current.state == State::Fault ||
        current.state == State::RecoveryCheck) {
        result.success = false;
        result.error = current.fault_latched_reason.empty()
            ? "gimbal_fault_active"
            : current.fault_latched_reason;
        return result;
    }
    if (!current.yaw_deg || !current.pitch_deg) {
        result.success = false;
        result.error = "alignment_rebase_angle_unknown";
        return result;
    }
    alignment_yaw_deg_ = normalizeAngle(*current.yaw_deg);
    alignment_pitch_deg_ = normalizeAngle(*current.pitch_deg);
    return result;
}

CommandResult GimbalHardwareService::freezeAtCurrent(int timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    CommandResult aggregate;
    if (timeout_ms <= 0) {
        aggregate.error = "invalid_freeze_timeout";
        return aggregate;
    }
    auto angle = refreshAngle(500);
    aggregate.transcript.insert(aggregate.transcript.end(),
                                angle.transcript.begin(),
                                angle.transcript.end());
    if (!angle.success) return angle;
    auto current = machine_.status();
    if (!current.yaw_deg || !current.pitch_deg) {
        tripFaultLocked("freeze_angle_unknown");
        aggregate.error = machine_.status().fault_latched_reason;
        return aggregate;
    }
    double target_yaw = *current.yaw_deg;
    double target_pitch = *current.pitch_deg;
    const auto deadline_ms = clock_() + std::max(timeout_ms, 1600);

    auto set_target = [&]() {
        auto result = moveTo(target_yaw, target_pitch, 500);
        aggregate.transcript.insert(aggregate.transcript.end(),
                                    result.transcript.begin(),
                                    result.transcript.end());
        if (!result.success) {
            aggregate.timeout = result.timeout;
            aggregate.error = result.error;
        }
        return result.success;
    };
    if (!set_target()) return aggregate;

    for (unsigned settle_pass = 0; settle_pass < 2; ++settle_pass) {
        const auto settle_deadline_ms =
            std::min(deadline_ms, clock_() + 800);
        std::optional<double> previous_yaw;
        std::optional<double> previous_pitch;
        bool mechanically_stable = false;
        unsigned samples = 0;
        while (clock_() < settle_deadline_ms && samples++ < 20) {
            sleeper_(std::chrono::milliseconds(80));
            angle = refreshAngle(500);
            aggregate.transcript.insert(aggregate.transcript.end(),
                                        angle.transcript.begin(),
                                        angle.transcript.end());
            if (!angle.success) return angle;
            current = machine_.status();
            if (!current.yaw_deg || !current.pitch_deg) {
                tripFaultLocked("freeze_angle_unknown");
                aggregate.error = machine_.status().fault_latched_reason;
                return aggregate;
            }
            const double final_error = std::max(
                std::abs(shortestAngleDelta(target_yaw, *current.yaw_deg)),
                std::abs(shortestAngleDelta(target_pitch, *current.pitch_deg)));
            if (previous_yaw && previous_pitch) {
                const double stability = std::max(
                    std::abs(shortestAngleDelta(*previous_yaw, *current.yaw_deg)),
                    std::abs(shortestAngleDelta(*previous_pitch, *current.pitch_deg)));
                if (stability <= 0.5) {
                    mechanically_stable = true;
                    if (final_error <= 1.5) {
                        aggregate.success = true;
                        aggregate.error.clear();
                        return aggregate;
                    }
                    break;
                }
            }
            previous_yaw = *current.yaw_deg;
            previous_pitch = *current.pitch_deg;
        }
        if (!mechanically_stable) {
            tripFaultLocked("freeze_not_stable");
            aggregate.error = machine_.status().fault_latched_reason;
            return aggregate;
        }
        if (settle_pass == 0) {
            target_yaw = *current.yaw_deg;
            target_pitch = *current.pitch_deg;
            if (!set_target()) return aggregate;
        }
    }
    tripFaultLocked("freeze_final_error_exceeded");
    aggregate.error = machine_.status().fault_latched_reason;
    return aggregate;
}

CommandResult GimbalHardwareService::trackTo(
    double yaw_deg, double pitch_deg, int timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    CommandResult result;
    if (!limits_.verified()) {
        result.error = "motion_limits_unverified";
        return result;
    }
    if (!alignment_yaw_deg_ || !alignment_pitch_deg_) {
        result.error = "alignment_reference_unknown";
        return result;
    }
    if (!limits_.contains(*alignment_yaw_deg_, *alignment_pitch_deg_, yaw_deg, pitch_deg)) {
        result.error = "target_out_of_bounds";
        return result;
    }
    if (!machine_.beginMove(clock_())) {
        result.error = "invalid_state:" + std::string(stateName(machine_.status().state));
        return result;
    }
    std::string error;
    if (!io_->writeLine(trackCommand(yaw_deg, pitch_deg), error)) {
        tripFaultLocked(error.empty() ? "track_write_failed" : error);
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    result = waitFor([](const Event& event) {
        return event.kind == EventKind::TrackAck;
    }, timeout_ms);
    if (result.success) {
        (void)machine_.markHolding(clock_());
    } else {
        tripFaultLocked(result.timeout ? "track_timeout" : result.error);
    }
    return result;
}

CommandResult GimbalHardwareService::hold(int timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    CommandResult result;
    const auto state = machine_.status().state;
    if (state != State::Ready && state != State::Moving && state != State::Holding) {
        result.error = "invalid_state:" + std::string(stateName(state));
        return result;
    }
    std::string error;
    if (!io_->writeLine(holdCommand(), error)) {
        tripFaultLocked(error.empty() ? "hold_write_failed" : error);
        result.error = machine_.status().fault_latched_reason;
        return result;
    }
    result = waitFor([](const Event& event) {
        return event.kind == EventKind::Ok && event.subject == "HOLD_ON";
    }, timeout_ms);
    if (result.success && state != State::Holding) (void)machine_.markHolding(clock_());
    if (!result.success) tripFaultLocked(result.timeout ? "hold_timeout" : result.error);
    return result;
}

CommandResult GimbalHardwareService::probeRecovery() {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    CommandResult result;
    if (!machine_.beginRecoveryProbe(clock_())) {
        result.error = "recovery_probe_not_due";
        return result;
    }
    std::string error;
    if (!setSafeLinesLocked(error)) {
        (void)machine_.finishRecoveryProbe(clock_(), false);
        result.error = error;
        return result;
    }
    bool fault_active = true;
    if (!io_->readFaultActive(fault_active, error) || fault_active) {
        (void)machine_.finishRecoveryProbe(clock_(), false);
        result.error = fault_active ? "pd6_low" : error;
        return result;
    }

    const struct ProbeStep {
        EventKind command;
        Matcher matcher;
    } steps[] = {
        {EventKind::Ping, isHealthyPingEvent},
        {EventKind::FaultStatus, isHealthyFaultEvent},
        {EventKind::Status, isHealthyStatusEvent},
    };
    for (const auto& step : steps) {
        if (!io_->writeLine(diagnosticCommand(step.command), error)) {
            (void)machine_.finishRecoveryProbe(clock_(), false);
            result.error = error;
            return result;
        }
        result = waitFor(step.matcher, 500);
        if (!result.success) {
            (void)machine_.finishRecoveryProbe(clock_(), false);
            return result;
        }
    }
    result.success = machine_.finishRecoveryProbe(clock_(), true) ||
                     machine_.status().state == State::Fault;
    return result;
}

void GimbalHardwareService::pollOnce() {
    std::lock_guard<std::recursive_mutex> lock(operation_mutex_);
    if (!initialized_) {
        sleeper_(std::chrono::milliseconds(25));
        return;
    }
    bool fault_active = false;
    std::string error;
    if (io_->readFaultActive(fault_active, error)) {
        const auto state = machine_.status().state;
        if (fault_active && state != State::Disabled && state != State::Fault &&
            state != State::RecoveryCheck) {
            tripFaultLocked("pd6_low");
        }
    } else if (machine_.status().state != State::Disabled) {
        tripFaultLocked(error.empty() ? "pd6_read_failed" : error);
    }

    if (machine_.recoveryProbeDue(clock_())) {
        (void)probeRecovery();
        return;
    }

    const auto active_state = machine_.status().state;
    const auto now_ms = clock_();
    if ((active_state == State::Ready || active_state == State::Moving ||
         active_state == State::Holding) &&
        (next_health_probe_ms_ == 0 || now_ms >= next_health_probe_ms_)) {
        next_health_probe_ms_ = now_ms + 500;
        if (!io_->writeLine(diagnosticCommand(EventKind::FaultStatus), error)) {
            ++consecutive_health_failures_;
        } else {
            Event fault;
            auto health = waitFor(isHealthyFaultEvent, 300, &fault);
            if (health.success) {
                consecutive_health_failures_ = 0;
                if (fault.uart_errors) {
                    if (last_uart_errors_ && *fault.uart_errors > *last_uart_errors_) {
                        LOGW("GimbalHardwareService", "G431 UARTERR increased: %u -> %u",
                             *last_uart_errors_, *fault.uart_errors);
                    }
                    last_uart_errors_ = fault.uart_errors;
                }
            } else {
                ++consecutive_health_failures_;
            }
        }
        if (consecutive_health_failures_ >= 3) {
            tripFaultLocked("gimbal_health_unresponsive");
            return;
        }
    }

    std::string bytes;
    if (io_->readBytes(bytes, 10, error)) {
        bool ignored = false;
        dispatchBytes(bytes, {}, nullptr, ignored, nullptr, nullptr);
        if (machine_.status().state == State::Fault) emergencyStopLocked();
    } else if (machine_.status().state != State::Disabled &&
               machine_.status().state != State::Fault) {
        tripFaultLocked(error.empty() ? "uart_read_failed" : error);
    }
    sleeper_(std::chrono::milliseconds(5));
}

}  // namespace edgefocus::gimbal
