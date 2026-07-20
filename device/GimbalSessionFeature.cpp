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
        setHealth(false, true, "recovering", error);
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
            if (!controller_.snapshot().manual_lease) {
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
                const bool healthy = status.state != PrecheckState::Degraded &&
                    (hardware.state == State::Ready || hardware.state == State::Holding ||
                     hardware.state == State::Moving);
                setHealth(healthy, !healthy, healthy ? "ready" : "recovering",
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
    projected.detail = status.detail;
    const auto hardware = controller_.snapshotHardware();
    projected.angle_x = hardware.yaw_deg;
    projected.angle_y = hardware.pitch_deg;
    projected.tracking = tracker_.status().tracking;
    projected.recoverable = hardware.recoverable;
    projected.fault_latched_reason = hardware.fault_latched_reason;
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
