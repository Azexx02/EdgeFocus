#include "device/GimbalSessionFeature.h"

#include <thread>
#include <utility>

namespace edgefocus::gimbal {

GimbalSessionFeature::GimbalSessionFeature(
    GimbalPrecheckController& controller,
    GimbalVisualTracker& tracker,
    session::SessionSnapshotPublisher& snapshots,
    Clock clock,
    Sleeper sleeper)
    : controller_(controller),
      tracker_(tracker),
      snapshots_(snapshots),
      clock_(clock ? std::move(clock) : Clock(defaultClockMs)),
      sleeper_(sleeper ? std::move(sleeper)
                       : Sleeper([](std::chrono::milliseconds delay) {
                             std::this_thread::sleep_for(delay);
                         })) {}

GimbalSessionFeature::~GimbalSessionFeature() {
    stop(session::StopReason::ProcessShutdown);
}

std::int64_t GimbalSessionFeature::defaultClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void GimbalSessionFeature::setHealth(
    bool available, bool degraded, std::string status, std::string detail) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    health_ = {available, degraded, std::move(status), std::move(detail), clock_()};
}

session::PrepareResult GimbalSessionFeature::prepare(const session::SessionContext& context) {
    if (running_.load()) return session::PrepareResult::failure("gimbal feature already running");
    std::string error;
    const bool ready = controller_.start(context.session_id, clock_(), &error);
    phase_.store(session::Phase::Idle);
    tracking_accept_after_ms_.store(0);
    running_.store(true);
    worker_ = std::thread(&GimbalSessionFeature::workerLoop, this);
    if (!ready) {
        const auto status = controller_.snapshot();
        setHealth(false, true,
                  status.state == PrecheckState::MaintenanceRequired
                      ? "maintenance_required"
                      : "recovering",
                  !status.detail.empty() ? status.detail : error);
        return session::PrepareResult::degraded_success(error);
    }
    setHealth(true, false, "ready");
    return session::PrepareResult::success();
}

void GimbalSessionFeature::enterPhase(session::Phase phase) {
    const auto now_ms = clock_();
    if (phase == session::Phase::Focusing) {
        // Publish the fresh-evidence gate before the worker can observe the
        // FOCUSING phase. Frames captured during PRECHECK/manual positioning
        // must never drive formal-session tracking.
        tracking_accept_after_ms_.store(now_ms);
    }
    phase_.store(phase);
    if (phase == session::Phase::Focusing || phase == session::Phase::Paused) {
        controller_.holdForSession(now_ms);
        tracker_.reset();
    } else if (phase == session::Phase::Ending) {
        controller_.stop();
    }
    publish(now_ms);
}

void GimbalSessionFeature::workerLoop() {
    std::int64_t last_health_check_ms = 0;
    while (running_.load()) {
        const auto now_ms = clock_();
        const auto current_phase = phase_.load();
        if (current_phase == session::Phase::Precheck) {
            controller_.observe(now_ms, snapshots_.read().visual);
        } else if (current_phase == session::Phase::Focusing) {
            // A browser/App manual lease temporarily owns motion.  The formal
            // session and visual inference continue, but automatic tracking
            // must not race manual nudge commands on the shared UART.
            const auto control = controller_.snapshot();
            const bool tracking_allowed =
                !control.manual_lease &&
                control.state != PrecheckState::MaintenanceRequired &&
                control.state != PrecheckState::Released &&
                control.state != PrecheckState::Degraded &&
                control.state != PrecheckState::Inactive;
            if (tracking_allowed) {
                const auto visual = snapshots_.read().visual;
                if (visual.evidence_timestamp_ms >
                    tracking_accept_after_ms_.load()) {
                    (void)tracker_.update(now_ms, visual);
                }
            }
        }
        if (current_phase == session::Phase::Precheck ||
            current_phase == session::Phase::Focusing ||
            current_phase == session::Phase::Paused) {
            if (last_health_check_ms == 0 || now_ms - last_health_check_ms >= 250) {
                controller_.maintain(current_phase, now_ms);
                const auto status = controller_.snapshot();
                const auto hardware = controller_.snapshotHardware();
                const bool controller_available =
                    status.state != PrecheckState::Degraded &&
                    status.state != PrecheckState::MaintenanceRequired &&
                    status.state != PrecheckState::Released &&
                    status.state != PrecheckState::Inactive;
                const bool healthy = controller_available &&
                    (hardware.state == State::Ready || hardware.state == State::Holding ||
                     hardware.state == State::Moving);
                const char* unhealthy_status =
                    status.state == PrecheckState::MaintenanceRequired
                        ? "maintenance_required"
                        : status.state == PrecheckState::Released
                              ? "released"
                              : "recovering";
                setHealth(healthy, !healthy, healthy ? "ready" : unhealthy_status,
                          healthy ? status.detail
                                  : (!status.detail.empty() ? status.detail
                                                            : hardware.fault_latched_reason));
                publish(now_ms);
                last_health_check_ms = now_ms;
            }
        }
        sleeper_(std::chrono::milliseconds(50));
    }
}

void GimbalSessionFeature::publish(std::int64_t now_ms) {
    const auto status = controller_.snapshot();
    session::GimbalSnapshot projected;
    projected.state = precheckStateName(status.state);
    projected.health = health();
    projected.manual_lease = status.manual_lease;
    projected.search_generation = status.generation;
    projected.waypoint_index = static_cast<std::uint32_t>(status.waypoint_index);
    projected.subject_found = status.subject_found;
    projected.search_completed = status.search_completed;
    projected.lease_owner_id = status.lease_owner_id;
    projected.lease_epoch = status.lease_epoch;
    projected.lease_expires_at_ms = status.lease_expires_at_ms;
    projected.detail = status.detail;
    const auto hardware = controller_.snapshotHardware();
    const auto& wire = hardware.snapshot;
    projected.protocol_version = wire.protocol_version;
    projected.capabilities = wire.capabilities;
    projected.available = wire.available;
    projected.degraded_reason = wire.degraded_reason;
    switch (wire.link_health) {
        case LinkHealth::Unavailable: projected.link_health = "unavailable"; break;
        case LinkHealth::Negotiating: projected.link_health = "negotiating"; break;
        case LinkHealth::Healthy: projected.link_health = "healthy"; break;
        case LinkHealth::Degraded: projected.link_health = "degraded"; break;
    }
    projected.control_mode = controlModeName(wire.control_mode);
    projected.angle_x = hardware.yaw_deg;
    projected.angle_y = hardware.pitch_deg;
    projected.current_yaw10 = wire.current_yaw10;
    projected.current_pitch10 = wire.current_pitch10;
    projected.target_yaw10 = wire.target_yaw10;
    projected.target_pitch10 = wire.target_pitch10;
    projected.motion_generation = wire.motion_generation;
    projected.settled = wire.settled;
    projected.tracking = tracker_.status().tracking;
    projected.calibration_valid = wire.calibration_valid;
    projected.calibration_epoch = wire.calibration_epoch;
    projected.origin_valid = wire.origin_valid;
    projected.origin_epoch = wire.origin_epoch;
    projected.recoverable = hardware.recoverable;
    projected.fault_class = errorClassName(wire.fault_class);
    projected.fault_code = wire.fault_code;
    projected.fault_latched_reason = hardware.fault_latched_reason;
    projected.last_pose_age_ms = wire.last_pose_age_ms;
    projected.last_queued_sequence = wire.last_queued_sequence;
    projected.acked_through_sequence = wire.acked_through_sequence;
    projected.current_protection_state = wire.current_protection_state;
    snapshots_.updateGimbal(projected, now_ms);
}

void GimbalSessionFeature::stop(session::StopReason /*reason*/) noexcept {
    if (running_.exchange(false) && worker_.joinable()) worker_.join();
    controller_.stop();
    tracker_.reset();
    phase_.store(session::Phase::Idle);
    tracking_accept_after_ms_.store(0);
    setHealth(false, false, "stopped");
}

session::FeatureHealth GimbalSessionFeature::health() const {
    std::lock_guard<std::mutex> lock(health_mutex_);
    return health_;
}

}  // namespace edgefocus::gimbal
