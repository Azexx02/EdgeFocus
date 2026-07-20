#include "platform/gimbal/GimbalHardwareService.h"

#include "app/AppContext.h"
#include "app/Logger.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <thread>
#include <utility>

namespace edgefocus::gimbal {
namespace {

CommandResult failure(std::string error, ErrorClass error_class,
                      std::string error_code = {}) {
    CommandResult result;
    result.error = std::move(error);
    result.error_class = error_class;
    result.error_code = std::move(error_code);
    return result;
}

std::int32_t degreeToTenths(double degree) {
    return static_cast<std::int32_t>(std::lround(normalizeAngle(degree) * 10.0)) % 3600;
}

}  // namespace

std::int64_t GimbalHardwareService::defaultClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

GimbalHardwareService::GimbalHardwareService(std::unique_ptr<IGimbalIo> io,
                                             MotionLimits limits, Clock clock,
                                             Sleeper sleeper)
    : Module("GimbalHardwareService"),
      io_(std::move(io)),
      limits_(std::move(limits)),
      clock_(clock ? std::move(clock) : Clock(defaultClockMs)),
      sleeper_(sleeper ? std::move(sleeper)
                       : Sleeper([](std::chrono::milliseconds delay) {
                             std::this_thread::sleep_for(delay);
                         })) {
    snapshot_.control_mode = io_ ? io_->controlMode() : ControlMode::UartOnly;
    published_status_.snapshot = snapshot_;
}

GimbalHardwareService::~GimbalHardwareService() { stop(); }

bool GimbalHardwareService::init(AppContext& /*ctx*/) {
    std::string error;
    if (!start(&error)) {
        next_start_attempt_ms_ = clock_() + StateMachine::kRecoveryIntervalMs;
        LOGW("GimbalHardwareService",
             "Initialization degraded; fixed-camera mode remains available: %s",
             error.c_str());
    }
    return true;
}

void GimbalHardwareService::runOnce(AppContext& /*ctx*/) {
    const auto current = snapshot();
    if (!current.available && current.link_health == LinkHealth::Unavailable) {
        const auto now_ms = clock_();
        if (now_ms >= next_start_attempt_ms_) {
            std::string error;
            if (!start(&error)) {
                next_start_attempt_ms_ = now_ms + StateMachine::kRecoveryIntervalMs;
            } else {
                next_start_attempt_ms_ = 0;
            }
        }
    } else {
        pollOnce();
    }
    sleeper_(std::chrono::milliseconds(10));
}

void GimbalHardwareService::shutdown(AppContext& /*ctx*/) { stop(); }

void GimbalHardwareService::ensureWorker() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (worker_.joinable()) return;
    worker_terminate_ = false;
    worker_ = std::thread([this] { workerMain(); });
}

void GimbalHardwareService::workerMain() {
    worker_id_ = std::this_thread::get_id();
    for (;;) {
        std::unique_ptr<QueueItem> item;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return worker_terminate_ || emergency_requested_.load() || !queue_.empty();
            });
            if (emergency_requested_.exchange(false)) {
                lock.unlock();
                emergencyStopLocked();
                continue;
            }
            if (worker_terminate_ && queue_.empty()) break;
            item = std::move(queue_.front());
            queue_.pop_front();
        }

        CommandResult result;
        try {
            result = item->work();
        } catch (const std::exception& exception) {
            result = failure(std::string("worker_exception:") + exception.what(),
                             ErrorClass::Link, "WORKER_EXCEPTION");
            degradeLocked(result.error, result.error_class, result.error_code, true);
        } catch (...) {
            result = failure("worker_exception:unknown", ErrorClass::Link,
                             "WORKER_EXCEPTION");
            degradeLocked(result.error, result.error_class, result.error_code, true);
        }
        result.queued = true;
        result.request_sequence = item->sequence;
        snapshot_.last_queued_sequence = item->sequence;
        snapshot_.acked_through_sequence = item->sequence;
        publishLocked();
        item->completion.set_value(std::move(result));
    }
    worker_id_ = {};
}

CommandResult GimbalHardwareService::submit(Work work, int caller_timeout_ms) {
    if (std::this_thread::get_id() == worker_id_) return work();
    ensureWorker();
    auto item = std::make_unique<QueueItem>();
    item->sequence = next_sequence_.fetch_add(1);
    item->work = std::move(work);
    auto future = item->completion.get_future();
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= kMaximumQueueDepth) {
            return failure("gimbal_queue_full", ErrorClass::Transient, "QUEUE_FULL");
        }
        {
            std::lock_guard<std::mutex> published_lock(published_mutex_);
            published_status_.snapshot.last_queued_sequence = item->sequence;
        }
        queue_.push_back(std::move(item));
    }
    queue_cv_.notify_one();
    if (future.wait_for(std::chrono::milliseconds(std::max(100, caller_timeout_ms))) !=
        std::future_status::ready) {
        auto result = failure("gimbal_worker_deadline", ErrorClass::Timeout,
                              "WORKER_DEADLINE");
        result.timeout = true;
        return result;
    }
    return future.get();
}

bool GimbalHardwareService::start(std::string* error) {
    const auto result = submit([this] { return openLocked(); }, 3000);
    if (error) *error = result.error;
    return result.success;
}

void GimbalHardwareService::stop() noexcept {
    if (!worker_.joinable()) return;
    (void)submit([this] { return closeLocked(); }, 3000);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        worker_terminate_ = true;
    }
    queue_cv_.notify_all();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
}

void GimbalHardwareService::requestEmergencyStop() noexcept {
    emergency_requested_.store(true);
    queue_cv_.notify_one();
}

Status GimbalHardwareService::status() const {
    std::lock_guard<std::mutex> lock(published_mutex_);
    auto result = published_status_;
    if (result.snapshot.last_pose_at_ms > 0) {
        result.snapshot.last_pose_age_ms =
            std::max<std::int64_t>(0, defaultClockMs() - result.snapshot.last_pose_at_ms);
    }
    return result;
}

GimbalSnapshot GimbalHardwareService::snapshot() const { return status().snapshot; }

bool GimbalHardwareService::setSafeLinesLocked(std::string& error) {
    if (!io_->setBoot0(false, error)) return false;
    return io_->setEnable(false, error);
}

CommandResult GimbalHardwareService::openLocked() {
    if (initialized_) return CommandResult{true};
    std::string error;
    if (!io_ || !io_->open(error) || !setSafeLinesLocked(error)) {
        if (io_) io_->close();
        snapshot_.available = false;
        snapshot_.link_health = LinkHealth::Unavailable;
        snapshot_.degraded_reason = error.empty() ? "gimbal_io_open_failed" : error;
        publishLocked();
        return failure(snapshot_.degraded_reason, ErrorClass::Link, "IO_OPEN_FAILED");
    }
    initialized_ = true;
    framer_.reset();
    machine_.reset(clock_());
    snapshot_ = GimbalSnapshot{};
    snapshot_.control_mode = io_->controlMode();
    snapshot_.link_health = LinkHealth::Negotiating;
    next_health_probe_ms_ = 0;
    consecutive_health_failures_ = 0;
    last_uart_errors_.reset();
    publishLocked();
    return CommandResult{true};
}

CommandResult GimbalHardwareService::closeLocked() {
    CommandResult result{true};
    if (!io_) return result;
    if (initialized_) {
        const auto state = machine_.status().state;
        if (state == State::Ready || state == State::Moving || state == State::Holding ||
            state == State::Calibrating) {
            result = stopAndDisableLocked(500);
        }
        std::string ignored;
        (void)io_->setEnable(false, ignored);
        io_->close();
    }
    initialized_ = false;
    snapshot_.available = false;
    snapshot_.link_health = LinkHealth::Unavailable;
    snapshot_.state = State::Disabled;
    publishLocked();
    return result;
}

void GimbalHardwareService::publishLocked() {
    auto value = machine_.status();
    snapshot_.state = value.state;
    snapshot_.available = initialized_ &&
        snapshot_.compatibility == ProtocolCompatibility::Protocol3Control &&
        snapshot_.link_health == LinkHealth::Healthy && value.state != State::Fault;
    value.snapshot = snapshot_;
    value.link_healthy = snapshot_.link_health == LinkHealth::Healthy;
    value.yaw_deg = snapshot_.current_yaw10
        ? std::optional<double>(*snapshot_.current_yaw10 / 10.0) : std::nullopt;
    value.pitch_deg = snapshot_.current_pitch10
        ? std::optional<double>(*snapshot_.current_pitch10 / 10.0) : std::nullopt;
    std::lock_guard<std::mutex> lock(published_mutex_);
    published_status_ = std::move(value);
}

void GimbalHardwareService::applyEventLocked(const Event& event) {
    (void)machine_.onEvent(event, clock_());
    if (!event.protocol_version.empty()) snapshot_.protocol_version = event.protocol_version;
    if (!event.capabilities.empty()) snapshot_.capabilities = event.capabilities;
    if (event.arm_profile) snapshot_.arm_profile = *event.arm_profile;
    if (event.yaw_deg) snapshot_.current_yaw10 = degreeToTenths(*event.yaw_deg);
    if (event.pitch_deg) snapshot_.current_pitch10 = degreeToTenths(*event.pitch_deg);
    if (event.target_yaw_deg) snapshot_.target_yaw10 = degreeToTenths(*event.target_yaw_deg);
    if (event.target_pitch_deg) snapshot_.target_pitch10 = degreeToTenths(*event.target_pitch_deg);
    if (event.motion_generation) snapshot_.motion_generation = event.motion_generation;
    if (event.settled) snapshot_.settled = *event.settled;
    if (event.calibration_valid) snapshot_.calibration_valid = *event.calibration_valid;
    if (event.origin_valid) snapshot_.origin_valid = *event.origin_valid;
    if (event.calibration_epoch) snapshot_.calibration_epoch = *event.calibration_epoch;
    if (event.origin_epoch) snapshot_.origin_epoch = *event.origin_epoch;
    if (!event.current_protection_state.empty()) {
        snapshot_.current_protection_state = event.current_protection_state;
    }
    if (event.kind == EventKind::Pose) snapshot_.last_pose_at_ms = clock_();
    if (event.kind == EventKind::Error) {
        snapshot_.fault_class = event.error_class;
        snapshot_.fault_code = event.error_code;
    }
    if (event.kind == EventKind::FaultStatus && event.fault_active.value_or(false)) {
        snapshot_.fault_class = ErrorClass::Fault;
        snapshot_.fault_code = "G431_FAULT_ACTIVE";
    }
    publishLocked();
}

void GimbalHardwareService::dispatchBytesLocked(
    const std::string& bytes, const Matcher& matcher, Event* matched,
    bool& matched_result, Event* device_error, std::vector<std::string>* transcript) {
    for (const auto& line : framer_.feed(bytes)) {
        const auto event = parseEvent(line);
        if (transcript) transcript->push_back(event.raw);
        applyEventLocked(event);
        if (device_error && device_error->raw.empty() && event.kind == EventKind::Error) {
            *device_error = event;
        }
        if (!matched_result && matcher && matcher(event)) {
            matched_result = true;
            if (matched) *matched = event;
        }
    }
}

CommandResult GimbalHardwareService::waitForLocked(
    const Matcher& matcher, int timeout_ms, Event* matched, bool enforce_fault_pin) {
    CommandResult result;
    const auto deadline = clock_() + std::max(1, timeout_ms);
    while (clock_() <= deadline) {
        if (emergency_requested_.exchange(false)) {
            emergencyStopLocked();
            return failure("emergency_stop", ErrorClass::Safety, "EMERGENCY_STOP");
        }
        bool fault_active = false;
        std::string error;
        if (!io_->readFaultActive(fault_active, error)) {
            return failure(error.empty() ? "fault_pin_read_failed" : error,
                           ErrorClass::Link, "FAULT_PIN_READ_FAILED");
        }
        if (fault_active && enforce_fault_pin) {
            tripFaultLocked("pd6_low", ErrorClass::Fault, "FAULT_PIN_ACTIVE");
            return failure("pd6_low", ErrorClass::Fault, "FAULT_PIN_ACTIVE");
        }

        std::string bytes;
        if (!io_->readBytes(bytes, 25, error)) {
            return failure(error.empty() ? "uart_read_failed" : error,
                           ErrorClass::Link, "UART_READ_FAILED");
        }
        bool found = false;
        Event device_error;
        dispatchBytesLocked(bytes, matcher, matched, found, &device_error, &result.transcript);
        if (!device_error.raw.empty()) {
            result.error_class = device_error.error_class;
            result.error_code = device_error.error_code;
            result.error = "device_error:" + device_error.raw;
            return result;
        }
        if (enforce_fault_pin && machine_.status().state == State::Fault) {
            return failure(machine_.status().fault_latched_reason,
                           ErrorClass::Fault, snapshot_.fault_code);
        }
        if (found) {
            result.success = true;
            return result;
        }
        if (bytes.empty()) sleeper_(std::chrono::milliseconds(1));
    }
    result.timeout = true;
    result.error_class = ErrorClass::Timeout;
    result.error_code = "RESPONSE_TIMEOUT";
    result.error = "timeout";
    return result;
}

CommandResult GimbalHardwareService::sendAndWaitLocked(
    const std::string& command, const Matcher& matcher, int timeout_ms,
    Event* matched, bool enforce_fault_pin) {
    std::string error;
    if (!io_->writeLine(command, error)) {
        return failure(error.empty() ? "uart_write_failed" : error,
                       ErrorClass::Link, "UART_WRITE_FAILED");
    }
    return waitForLocked(matcher, timeout_ms, matched, enforce_fault_pin);
}

void GimbalHardwareService::degradeLocked(std::string reason,
                                          ErrorClass error_class,
                                          std::string error_code,
                                          bool hard_fault) {
    snapshot_.available = false;
    snapshot_.link_health = LinkHealth::Degraded;
    snapshot_.degraded_reason = std::move(reason);
    snapshot_.fault_class = error_class;
    snapshot_.fault_code = std::move(error_code);
    if (hard_fault) machine_.tripFault(clock_(), snapshot_.degraded_reason);
    publishLocked();
}

void GimbalHardwareService::tripFaultLocked(const std::string& reason,
                                            ErrorClass error_class,
                                            const std::string& error_code) {
    emergencyStopLocked();
    machine_.tripFault(clock_(), reason);
    snapshot_.link_health = LinkHealth::Degraded;
    snapshot_.degraded_reason = reason;
    snapshot_.fault_class = error_class;
    snapshot_.fault_code = error_code;
    publishLocked();
}

void GimbalHardwareService::emergencyStopLocked() noexcept {
    std::string ignored;
    if (io_ && initialized_) {
        (void)io_->writeEmergencyStop(ignored);
        (void)io_->setEnable(false, ignored);
    }
    machine_.reset(clock_());
    snapshot_.available = false;
    snapshot_.link_health = LinkHealth::Degraded;
    snapshot_.degraded_reason = "emergency_stop_resync_required";
    snapshot_.state = State::Disabled;
    publishLocked();
}

CommandResult GimbalHardwareService::negotiateLocked(int timeout_ms) {
    if (!initialized_) return failure("not_initialized", ErrorClass::Link, "NOT_INITIALIZED");
    machine_.reset(clock_());
    if (!machine_.beginLinkCheck(clock_())) {
        return failure("link_check_state_failed", ErrorClass::Command, "INVALID_STATE");
    }
    snapshot_.link_health = LinkHealth::Negotiating;
    snapshot_.available = false;
    publishLocked();

    CommandResult aggregate;
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto ping = sendAndWaitLocked("PING", isHealthyPingEvent,
                                     std::min(500, timeout_ms), nullptr, false);
        aggregate.transcript.insert(aggregate.transcript.end(), ping.transcript.begin(),
                                    ping.transcript.end());
        if (ping.success) break;
        if (attempt == 2) {
            degradeLocked("gimbal_link_unresponsive", ErrorClass::Link,
                          "PING_TIMEOUT", false);
            ping.transcript = std::move(aggregate.transcript);
            return ping;
        }
        sleeper_(std::chrono::milliseconds(100));
    }

    Event version;
    auto version_result = sendAndWaitLocked(
        "VER", [](const Event& event) { return event.kind == EventKind::Version; },
        std::min(500, timeout_ms), &version, false);
    aggregate.transcript.insert(aggregate.transcript.end(),
                                version_result.transcript.begin(),
                                version_result.transcript.end());
    if (!version_result.success) {
        degradeLocked("gimbal_version_unavailable", ErrorClass::Link,
                      "VERSION_TIMEOUT", false);
        version_result.transcript = std::move(aggregate.transcript);
        return version_result;
    }

    snapshot_.protocol_version = version.protocol_version;
    snapshot_.capabilities = version.capabilities;
    if (version.protocol_version == "2.0") {
        snapshot_.compatibility = ProtocolCompatibility::Protocol2DiagnosticOnly;
        snapshot_.link_health = LinkHealth::Degraded;
        snapshot_.degraded_reason = "protocol2_diagnostic_only";
        machine_.reset(clock_());
        publishLocked();
        auto result = failure("protocol2_diagnostic_only", ErrorClass::Business,
                              "PROTOCOL2_READ_ONLY");
        result.transcript = std::move(aggregate.transcript);
        return result;
    }
    if (!hasRequiredProtocol3Capabilities(version)) {
        snapshot_.compatibility = ProtocolCompatibility::Unknown;
        degradeLocked("protocol3_capability_mismatch", ErrorClass::Link,
                      "CAPABILITY_MISMATCH", false);
        auto result = failure("protocol3_capability_mismatch", ErrorClass::Link,
                              "CAPABILITY_MISMATCH");
        result.transcript = std::move(aggregate.transcript);
        return result;
    }

    auto hello = sendAndWaitLocked(
        helloCommand(), [](const Event& event) { return event.kind == EventKind::HelloAck; },
        std::min(500, timeout_ms), nullptr, false);
    aggregate.transcript.insert(aggregate.transcript.end(), hello.transcript.begin(),
                                hello.transcript.end());
    if (!hello.success) {
        degradeLocked("protocol3_negotiation_failed", ErrorClass::Link,
                      "HELLO_FAILED", false);
        hello.transcript = std::move(aggregate.transcript);
        return hello;
    }
    snapshot_.compatibility = ProtocolCompatibility::Protocol3Control;
    snapshot_.link_health = LinkHealth::Healthy;
    snapshot_.degraded_reason.clear();
    (void)machine_.markLinkHealthy(clock_());

    auto pose = refreshPoseLocked(std::min(500, timeout_ms));
    aggregate.transcript.insert(aggregate.transcript.end(), pose.transcript.begin(),
                                pose.transcript.end());
    if (!pose.success) {
        degradeLocked("initial_pose_failed", pose.error_class, pose.error_code, false);
        pose.transcript = std::move(aggregate.transcript);
        return pose;
    }
    aggregate.success = true;
    publishLocked();
    return aggregate;
}

CommandResult GimbalHardwareService::enableAndAlign(int timeout_ms) {
    return submit([this, timeout_ms] { return enableAndAlignLocked(timeout_ms); },
                  timeout_ms + 3000);
}

CommandResult GimbalHardwareService::enableAndAlignLocked(int timeout_ms) {
    auto result = negotiateLocked(timeout_ms);
    if (!result.success) return result;
    if (!machine_.beginAlignment(clock_())) {
        return failure("arm_state_failed", ErrorClass::Command, "INVALID_STATE");
    }
    std::string error;
    if (io_->controlMode() == ControlMode::UartGpioInterlock) {
        if (!io_->setEnable(true, error)) {
            tripFaultLocked(error.empty() ? "enable_failed" : error,
                            ErrorClass::Link, "ENABLE_FAILED");
            return failure(machine_.status().fault_latched_reason,
                           ErrorClass::Link, "ENABLE_FAILED");
        }
        sleeper_(std::chrono::milliseconds(100));
    }
    Event arm;
    auto armed = sendAndWaitLocked(
        armCommand(), [](const Event& event) { return event.kind == EventKind::ArmAck; },
        std::min(timeout_ms, 2000), &arm);
    result.transcript.insert(result.transcript.end(), armed.transcript.begin(),
                             armed.transcript.end());
    if (!armed.success) {
        if (armed.error_class == ErrorClass::Fault ||
            armed.error_class == ErrorClass::Safety ||
            armed.error_class == ErrorClass::Link) {
            tripFaultLocked(armed.error.empty() ? "arm_failed" : armed.error,
                            armed.error_class, armed.error_code);
        } else {
            degradeLocked(armed.error, armed.error_class, armed.error_code, false);
        }
        armed.transcript = std::move(result.transcript);
        return armed;
    }
    (void)machine_.markReady(clock_());
    snapshot_.arm_profile = ArmProfile::Production;
    if (arm.calibration_epoch) snapshot_.calibration_epoch = *arm.calibration_epoch;
    if (arm.origin_epoch) snapshot_.origin_epoch = *arm.origin_epoch;
    auto pose = refreshPoseLocked(500);
    result.transcript.insert(result.transcript.end(), pose.transcript.begin(),
                             pose.transcript.end());
    if (!pose.success) return pose;
    result.success = true;
    result.firmware_accepted = true;
    result.settled = snapshot_.settled;
    publishLocked();
    return result;
}

bool GimbalHardwareService::motionAllowedLocked(CommandResult& result) const {
    if (snapshot_.compatibility != ProtocolCompatibility::Protocol3Control) {
        result = failure(snapshot_.compatibility == ProtocolCompatibility::Protocol2DiagnosticOnly
                             ? "protocol2_diagnostic_only" : "protocol_not_negotiated",
                         ErrorClass::Business, "MOTION_UNAVAILABLE");
        return false;
    }
    if (snapshot_.link_health != LinkHealth::Healthy) {
        result = failure("gimbal_link_degraded", ErrorClass::Link, "LINK_DEGRADED");
        return false;
    }
    return true;
}

CommandResult GimbalHardwareService::moveTo(double yaw_deg, double pitch_deg,
                                            int timeout_ms) {
    return submit([this, yaw_deg, pitch_deg, timeout_ms] {
        return moveToLocked(yaw_deg, pitch_deg, timeout_ms, true);
    }, timeout_ms + 1000);
}

CommandResult GimbalHardwareService::trackTo(double yaw_deg, double pitch_deg,
                                             int timeout_ms) {
    return submit([this, yaw_deg, pitch_deg, timeout_ms] {
        return moveToLocked(yaw_deg, pitch_deg, timeout_ms, true);
    }, timeout_ms + 1000);
}

CommandResult GimbalHardwareService::moveToLocked(double yaw_deg, double pitch_deg,
                                                  int timeout_ms,
                                                  bool enforce_limits) {
    CommandResult result;
    if (!motionAllowedLocked(result)) return result;
    if (enforce_limits && !limits_.verified()) {
        return failure("motion_limits_unverified", ErrorClass::Safety,
                       "LIMITS_UNVERIFIED");
    }
    if (enforce_limits && !limits_.contains(0.0, 0.0, yaw_deg, pitch_deg)) {
        return failure("target_out_of_bounds", ErrorClass::Safety,
                       "TARGET_OUT_OF_BOUNDS");
    }
    if (!machine_.beginMove(clock_())) {
        return failure("invalid_state:" + std::string(stateName(machine_.status().state)),
                       ErrorClass::Command, "INVALID_STATE");
    }
    Event accepted;
    result = sendAndWaitLocked(
        trackCommand(yaw_deg, pitch_deg),
        [](const Event& event) { return event.kind == EventKind::MotionAccepted; },
        timeout_ms, &accepted);
    if (!result.success) {
        if (result.error_class == ErrorClass::Fault ||
            result.error_class == ErrorClass::Link) {
            tripFaultLocked(result.error, result.error_class, result.error_code);
        }
        return result;
    }
    result.firmware_accepted = true;
    result.motion_generation = accepted.motion_generation;
    result.settled = false;
    snapshot_.target_yaw10 = degreeToTenths(yaw_deg);
    snapshot_.target_pitch10 = degreeToTenths(pitch_deg);
    snapshot_.motion_generation = accepted.motion_generation;
    snapshot_.settled = false;
    publishLocked();
    return result;
}

CommandResult GimbalHardwareService::moveToVerified(
    double yaw_deg, double pitch_deg, int timeout_ms, double tolerance_deg) {
    return submit([this, yaw_deg, pitch_deg, timeout_ms, tolerance_deg] {
        return moveToVerifiedLocked(yaw_deg, pitch_deg, timeout_ms,
                                    tolerance_deg, true);
    }, timeout_ms + 2000);
}

CommandResult GimbalHardwareService::moveToManualVerified(
    double yaw_deg, double pitch_deg, int timeout_ms, double tolerance_deg) {
    return submit([this, yaw_deg, pitch_deg, timeout_ms, tolerance_deg] {
        return moveToVerifiedLocked(yaw_deg, pitch_deg, timeout_ms,
                                    tolerance_deg, false);
    }, timeout_ms + 2000);
}

CommandResult GimbalHardwareService::moveToVerifiedLocked(
    double yaw_deg, double pitch_deg, int timeout_ms, double tolerance_deg,
    bool enforce_limits) {
    if (timeout_ms <= 0 || tolerance_deg <= 0.0 || tolerance_deg > 5.0) {
        return failure("invalid_move_verify_parameters", ErrorClass::Command,
                       "BAD_ARGUMENT");
    }
    auto aggregate = moveToLocked(yaw_deg, pitch_deg,
                                  std::min(timeout_ms, 1000), enforce_limits);
    if (!aggregate.success || !aggregate.motion_generation) return aggregate;
    const auto generation = *aggregate.motion_generation;
    const auto deadline = clock_() + timeout_ms;
    while (clock_() < deadline) {
        sleeper_(std::chrono::milliseconds(80));
        Event pose;
        auto current = sendAndWaitLocked(
            poseCommand(),
            [generation](const Event& event) {
                return event.kind == EventKind::Pose && event.motion_generation &&
                       *event.motion_generation == generation;
            }, std::min<int>(250, static_cast<int>(std::max<std::int64_t>(1, deadline - clock_()))),
            &pose);
        aggregate.transcript.insert(aggregate.transcript.end(),
                                    current.transcript.begin(), current.transcript.end());
        if (!current.success) {
            if (current.timeout) continue;
            current.transcript = std::move(aggregate.transcript);
            return current;
        }
        if (!pose.settled.value_or(false)) continue;
        if (!pose.yaw_deg || !pose.pitch_deg) continue;
        const double final_error = std::max(
            std::abs(shortestAngleDelta(*pose.yaw_deg, yaw_deg)),
            std::abs(shortestAngleDelta(*pose.pitch_deg, pitch_deg)));
        if (final_error > tolerance_deg) continue;
        (void)machine_.markHolding(clock_());
        snapshot_.settled = true;
        aggregate.success = true;
        aggregate.settled = true;
        aggregate.firmware_accepted = true;
        aggregate.error.clear();
        publishLocked();
        return aggregate;
    }
    aggregate.success = false;
    aggregate.timeout = true;
    aggregate.error_class = ErrorClass::Business;
    aggregate.error_code = "MOTION_NOT_SETTLED";
    aggregate.error = "motion_settle_timeout";
    return aggregate;
}

CommandResult GimbalHardwareService::refreshAngle(int timeout_ms) {
    return submit([this, timeout_ms] { return refreshPoseLocked(timeout_ms); },
                  timeout_ms + 1000);
}

CommandResult GimbalHardwareService::refreshPoseLocked(int timeout_ms) {
    if (!initialized_) return failure("not_initialized", ErrorClass::Link, "NOT_INITIALIZED");
    return sendAndWaitLocked(
        poseCommand(), [](const Event& event) { return event.kind == EventKind::Pose; },
        timeout_ms, nullptr, false);
}

CommandResult GimbalHardwareService::hold(int timeout_ms) {
    return submit([this, timeout_ms] { return holdLocked(timeout_ms); },
                  timeout_ms + 1000);
}

CommandResult GimbalHardwareService::holdLocked(int timeout_ms) {
    CommandResult unavailable;
    if (!motionAllowedLocked(unavailable)) return unavailable;
    const auto state = machine_.status().state;
    if (state != State::Ready && state != State::Moving && state != State::Holding) {
        return failure("invalid_state:" + std::string(stateName(state)),
                       ErrorClass::Command, "INVALID_STATE");
    }
    Event held;
    auto result = sendAndWaitLocked(
        holdCommand(), [](const Event& event) { return event.kind == EventKind::HoldAck; },
        timeout_ms, &held);
    if (!result.success) return result;
    if (state != State::Holding) (void)machine_.markHolding(clock_());
    result.firmware_accepted = true;
    result.motion_generation = held.motion_generation;
    result.settled = true;
    snapshot_.motion_generation = held.motion_generation;
    snapshot_.settled = true;
    publishLocked();
    return result;
}

CommandResult GimbalHardwareService::freezeAtCurrent(int timeout_ms) {
    return submit([this, timeout_ms] { return holdLocked(timeout_ms); },
                  timeout_ms + 1000);
}

CommandResult GimbalHardwareService::rebaseAlignmentAtCurrent(int /*timeout_ms*/) {
    return submit([] {
        CommandResult result;
        result.success = true;
        result.error_code = "SESSION_REBASE_NOOP_V3";
        return result;
    });
}

CommandResult GimbalHardwareService::stopAndDisable(int timeout_ms) {
    return submit([this, timeout_ms] { return stopAndDisableLocked(timeout_ms); },
                  timeout_ms + 1000);
}

CommandResult GimbalHardwareService::stopAndDisableLocked(int timeout_ms) {
    if (!initialized_) return failure("not_initialized", ErrorClass::Link, "NOT_INITIALIZED");
    const auto state = machine_.status().state;
    if (state == State::Ready || state == State::Moving || state == State::Holding) {
        (void)machine_.beginStop(clock_());
    }
    auto result = sendAndWaitLocked(
        stopCommand(), [](const Event& event) {
            return event.kind == EventKind::Ok && event.subject == "STOP";
        }, timeout_ms, nullptr, false);
    std::string error;
    const bool disabled = io_->setEnable(false, error);
    if (!result.success || !disabled) {
        emergencyStopLocked();
        if (!disabled) return failure(error, ErrorClass::Link, "ENABLE_LOW_FAILED");
        return result;
    }
    machine_.reset(clock_());
    snapshot_.state = State::Disabled;
    snapshot_.arm_profile = ArmProfile::None;
    snapshot_.settled = false;
    publishLocked();
    return result;
}

CommandResult GimbalHardwareService::probeDiagnostics(int timeout_ms_per_command) {
    return submit([this, timeout_ms_per_command] {
        return probeDiagnosticsLocked(timeout_ms_per_command);
    }, timeout_ms_per_command * 8 + 1000);
}

CommandResult GimbalHardwareService::probeDiagnosticsLocked(int timeout_ms_per_command) {
    if (!initialized_) return failure("not_initialized", ErrorClass::Link, "NOT_INITIALIZED");
    std::string error;
    if (!setSafeLinesLocked(error)) return failure(error, ErrorClass::Link, "SAFE_LINES_FAILED");
    auto result = negotiateLocked(timeout_ms_per_command);
    if (result.success) {
        for (const auto& step : std::vector<std::pair<std::string, Matcher>>{
                 {"FAULT", [](const Event& event) { return event.kind == EventKind::FaultStatus; }},
                 {"NVM STATUS", [](const Event& event) { return event.kind == EventKind::Nvm; }}}) {
            auto current = sendAndWaitLocked(step.first, step.second, timeout_ms_per_command,
                                             nullptr, false);
            result.transcript.insert(result.transcript.end(), current.transcript.begin(),
                                     current.transcript.end());
            if (!current.success) {
                result.success = false;
                result.failed_steps.push_back(step.first);
            }
        }
        return result;
    }
    if (snapshot_.compatibility != ProtocolCompatibility::Protocol2DiagnosticOnly) return result;

    result.error.clear();
    result.error_code.clear();
    result.error_class = ErrorClass::None;
    result.success = true;
    for (const auto& step : std::vector<std::pair<std::string, Matcher>>{
             {"STAT", [](const Event& event) { return event.kind == EventKind::Status; }},
             {"FAULT", [](const Event& event) { return event.kind == EventKind::FaultStatus; }},
             {"ANGLE", [](const Event& event) { return event.kind == EventKind::Angle; }}}) {
        auto current = sendAndWaitLocked(step.first, step.second, timeout_ms_per_command,
                                         nullptr, false);
        result.transcript.insert(result.transcript.end(), current.transcript.begin(),
                                 current.transcript.end());
        if (!current.success) {
            result.success = false;
            result.failed_steps.push_back(step.first);
        }
    }
    return result;
}

CommandResult GimbalHardwareService::probeRecovery() {
    return submit([this] { return probeRecoveryLocked(); }, 5000);
}

CommandResult GimbalHardwareService::probeRecoveryLocked() {
    if (!initialized_) return failure("not_initialized", ErrorClass::Link, "NOT_INITIALIZED");
    std::string error;
    if (!setSafeLinesLocked(error)) return failure(error, ErrorClass::Link, "SAFE_LINES_FAILED");
    machine_.reset(clock_());
    auto negotiated = negotiateLocked(1500);
    if (!negotiated.success) return negotiated;
    Event fault;
    auto fault_status = sendAndWaitLocked(
        "FAULT", [](const Event& event) { return event.kind == EventKind::FaultStatus; },
        500, &fault, false);
    negotiated.transcript.insert(negotiated.transcript.end(), fault_status.transcript.begin(),
                                 fault_status.transcript.end());
    if (!fault_status.success) return fault_status;
    if (fault.fault_active.value_or(false)) {
        auto cleared = sendAndWaitLocked(
            "FAULT CLEAR", [](const Event& event) {
                return event.kind == EventKind::Ok && event.subject == "FAULT_CLEAR";
            }, 1000, nullptr, false);
        negotiated.transcript.insert(negotiated.transcript.end(), cleared.transcript.begin(),
                                     cleared.transcript.end());
        if (!cleared.success) return cleared;
    }
    machine_.reset(clock_());
    snapshot_.link_health = LinkHealth::Healthy;
    snapshot_.degraded_reason.clear();
    snapshot_.fault_class = ErrorClass::None;
    snapshot_.fault_code.clear();
    snapshot_.available = true;
    publishLocked();
    negotiated.success = true;
    return negotiated;
}

void GimbalHardwareService::pollOnce() {
    (void)submit([this] {
        pollHardwareLocked();
        return CommandResult{true};
    }, 1000);
}

void GimbalHardwareService::pollHardwareLocked() {
    if (!initialized_) return;
    bool fault_active = false;
    std::string error;
    if (!io_->readFaultActive(fault_active, error)) {
        degradeLocked(error.empty() ? "fault_pin_read_failed" : error,
                      ErrorClass::Link, "FAULT_PIN_READ_FAILED", true);
        return;
    }
    if (fault_active) {
        tripFaultLocked("pd6_low", ErrorClass::Fault, "FAULT_PIN_ACTIVE");
        return;
    }
    std::string bytes;
    if (!io_->readBytes(bytes, 5, error)) {
        degradeLocked(error.empty() ? "uart_read_failed" : error,
                      ErrorClass::Link, "UART_READ_FAILED", true);
        return;
    }
    bool ignored = false;
    dispatchBytesLocked(bytes, {}, nullptr, ignored, nullptr, nullptr);

    const auto now_ms = clock_();
    if (snapshot_.compatibility == ProtocolCompatibility::Protocol3Control &&
        (next_health_probe_ms_ == 0 || now_ms >= next_health_probe_ms_)) {
        next_health_probe_ms_ = now_ms + 500;
        Event fault;
        auto health = sendAndWaitLocked(
            "FAULT", [](const Event& event) { return event.kind == EventKind::FaultStatus; },
            250, &fault, false);
        if (health.success) {
            consecutive_health_failures_ = 0;
            if (fault.uart_errors && last_uart_errors_ &&
                *fault.uart_errors > *last_uart_errors_) {
                LOGW("GimbalHardwareService", "G431 UARTERR increased: %u -> %u",
                     *last_uart_errors_, *fault.uart_errors);
            }
            if (fault.uart_errors) last_uart_errors_ = fault.uart_errors;
        } else if (++consecutive_health_failures_ >= 3) {
            degradeLocked("gimbal_health_unresponsive", ErrorClass::Link,
                          "HEALTH_TIMEOUT", true);
        }
    }
}

}  // namespace edgefocus::gimbal
