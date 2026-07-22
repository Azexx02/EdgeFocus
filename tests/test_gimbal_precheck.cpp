#include "device/GimbalBoardConfig.h"
#include "device/GimbalPrecheckController.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace edgefocus;
using namespace edgefocus::gimbal;

namespace {

class FakeHardware final : public IGimbalSessionHardware {
public:
    FakeHardware() {
        current.snapshot.calibration_valid = true;
        current.snapshot.origin_valid = true;
        current.snapshot.current_protection_state = "CONFIGURED";
    }

    Status status() const override { return current; }
    CommandResult enableAndAlign(int) override {
        calls.push_back("align");
        if (!align_success) {
            CommandResult result{false, false, align_error};
            result.error_code = align_code;
            return result;
        }
        current.state = State::Ready;
        current.recoverable = false;
        return {true};
    }
    CommandResult stopAndDisable(int) override {
        calls.push_back("disable");
        current.state = State::Disabled;
        return {true};
    }
    CommandResult moveTo(double yaw, double pitch, int) override {
        calls.push_back("move:" + std::to_string(yaw) + "," +
                        std::to_string(pitch));
        targets.push_back({yaw, pitch});
        if (!move_success) return {false, false, move_error};
        current.state = State::Holding;
        if (update_measurements) {
            current.yaw_deg = yaw;
            current.pitch_deg = pitch;
        }
        return {true};
    }
    CommandResult moveToVerified(double yaw, double pitch, int timeout_ms,
                                 double) override {
        return moveTo(yaw, pitch, timeout_ms);
    }
    CommandResult moveToManualVerified(double yaw, double pitch, int timeout_ms,
                                       double) override {
        return moveTo(yaw, pitch, timeout_ms);
    }
    CommandResult rebaseAlignmentAtCurrent(int) override {
        calls.push_back("rebase");
        return {true};
    }
    CommandResult trackTo(double yaw, double pitch, int timeout_ms) override {
        return moveTo(yaw, pitch, timeout_ms);
    }
    CommandResult hold(int) override {
        calls.push_back("hold");
        if (!hold_success) return {false, false, "hold_failed"};
        current.state = State::Holding;
        return {true};
    }
    CommandResult refreshAngle(int) override { return {true}; }
    CommandResult freezeAtCurrent(int) override {
        calls.push_back("freeze");
        return {true};
    }
    CommandResult recover(int) override {
        calls.push_back("recover");
        current.state = State::Disabled;
        current.recoverable = false;
        current.fault_active = false;
        current.fault_latched_reason.clear();
        return {true};
    }

    Status current{State::Ready, false, false, true, {}, 0.0, 0.0};
    bool align_success{true};
    std::string align_error{"align_failed"};
    std::string align_code;
    bool move_success{true};
    std::string move_error{"move_failed"};
    bool hold_success{true};
    bool update_measurements{true};
    std::vector<std::string> calls;
    std::vector<std::pair<double, double>> targets;
};

PrecheckConfig config() {
    PrecheckConfig value;
    value.search_waypoints = {{-5.0, 0.0, 1000}, {5.0, 0.0, 1000}};
    value.center = Waypoint{0.0, 0.0};
    value.normal_nudge_step_deg = 2.0;
    value.fine_nudge_step_deg = 1.0;
    return value;
}

session::VisualSnapshot subjectAt(std::int64_t at_ms,
                                  double center_x = 0.5,
                                  double center_y = 0.525) {
    session::VisualSnapshot visual;
    visual.frame_id = static_cast<std::uint64_t>(at_ms);
    visual.presence = 1.0;
    visual.evidence_timestamp_ms = at_ms;
    visual.subject_confidence = 0.9;
    visual.subject_box = session::VisualBoxSnapshot{
        center_x - 0.25, center_y - 0.325,
        center_x + 0.25, center_y + 0.325};
    visual.keypoints.resize(17);
    for (std::size_t index = 0; index < 10; ++index) {
        visual.keypoints[index] = {
            center_x - 0.02 + 0.01 * static_cast<double>(index % 3),
            center_y - 0.225 + 0.02 * static_cast<double>(index), 0.9};
    }
    return visual;
}

ManualCommandContext command(const PrecheckStatus& status,
                             std::uint64_t sequence,
                             std::string owner = "web") {
    return {status.session_id, std::move(owner), status.lease_token, sequence};
}

void test_board_config_is_protocol3_compact_profile() {
    const auto board = loadGimbalBoardConfig(
        std::string(EDGEFOCUS_SOURCE_DIR) + "/config/gimbal-board.json");
    assert(board.readyForSession());
    assert(board.precheck.hard_deadline_ms == 24000);
    assert(board.precheck.search_waypoints.size() == 4);
    assert(board.precheck.recovery_waypoints.empty());
    assert(board.precheck.normal_nudge_step_deg == 30.0);
    assert(board.precheck.fine_nudge_step_deg == 10.0);
    assert(!board.precheck.relative_to_alignment);
}

void test_missing_persistent_calibration_enters_maintenance() {
    FakeHardware hardware;
    hardware.current.snapshot.calibration_valid = false;
    GimbalPrecheckController controller(hardware, config());
    std::string error;
    assert(!controller.start("maintenance", 0, &error));
    assert(error == "calibration_required");
    assert(controller.snapshot().state == PrecheckState::MaintenanceRequired);
}

void test_precheck_holds_and_centers_before_searching() {
    FakeHardware hardware;
    hardware.current.yaw_deg = 35.0;
    hardware.current.pitch_deg = -20.0;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("prepared", 0));
    assert(controller.snapshot().state == PrecheckState::Searching);
    assert(hardware.calls.size() >= 2);
    assert(hardware.calls[0] == "hold");
    assert(hardware.calls[1] == "move:0.000000,0.000000");
    assert(hardware.targets.size() == 1);
    assert(hardware.targets[0].first == 0.0);
    assert(hardware.targets[0].second == 0.0);
}

void test_precheck_does_not_publish_searching_when_hold_fails() {
    FakeHardware hardware;
    hardware.hold_success = false;
    GimbalPrecheckController controller(hardware, config());
    std::string error;
    assert(!controller.start("hold-failure", 0, &error));
    assert(error == "hold_failed");
    assert(controller.snapshot().state == PrecheckState::Degraded);
    assert(hardware.calls.size() == 2);
    assert(hardware.calls[0] == "hold");
    assert(hardware.calls[1] == "disable");
}

void test_candidate_requires_distinct_stable_frames_and_no_implicit_lease() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("candidate", 0));
    const auto first = subjectAt(100);
    controller.observe(100, first);
    controller.observe(200, first);
    assert(controller.snapshot().state == PrecheckState::Searching);
    controller.observe(500, subjectAt(500));
    const auto status = controller.snapshot();
    assert(status.state == PrecheckState::ManualAvailable);
    assert(status.subject_found && status.search_completed);
    assert(!status.manual_lease);
    assert(hardware.calls[3] == "freeze");
    assert(hardware.calls[4] == "hold");
}

void test_search_waits_for_verified_waypoints_then_returns_origin() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("search", 0));
    controller.observe(600, {});
    controller.observe(2000, {});
    controller.observe(3000, {});
    const auto status = controller.snapshot();
    assert(status.state == PrecheckState::ManualAvailable);
    assert(!status.subject_found && !status.manual_lease);
    assert(hardware.targets.size() == 4);
    assert(hardware.targets[0].first == 0.0);
    assert(hardware.targets[1].first == -5.0);
    assert(hardware.targets[2].first == 5.0);
    assert(hardware.targets[3].first == 0.0);
}

void test_manual_lease_owner_ttl_and_idempotency() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("lease", 0));
    assert(controller.skipAuto("lease", 1, 50).success);
    assert(controller.snapshot().state == PrecheckState::ManualAvailable);

    PrecheckStatus issued;
    assert(controller.manualTakeover("lease", "web", 1,
                                     session::Phase::Precheck, 100,
                                     &issued).success);
    assert(issued.manual_lease && !issued.lease_token.empty());
    const auto before = hardware.targets.size();
    assert(controller.nudge(command(issued, 2), NudgeDirection::Right, 200).success);
    assert(hardware.targets.size() == before + 1);
    assert(controller.nudge(command(issued, 2), NudgeDirection::Right, 201).success);
    assert(hardware.targets.size() == before + 1);
    const auto conflict = controller.manualTakeover(
        "lease", "app", 1, session::Phase::Precheck, 300);
    assert(!conflict.success &&
           conflict.error == "manual_lease_owned_by_other_client");
    const auto targets_after_conflict = hardware.targets.size();
    assert(controller.nudge(command(issued, 2), NudgeDirection::Right, 301).success);
    assert(hardware.targets.size() == targets_after_conflict);
    const auto stale = controller.nudge(
        command(issued, 1), NudgeDirection::Right, 302);
    assert(!stale.success && stale.error == "STALE_SEQUENCE");
    assert(controller.keepManualLease(command(issued, 3),
                                      session::Phase::Precheck, 5000).success);
    const auto late_conflict = controller.manualTakeover(
        "lease", "app", 2, session::Phase::Precheck, 5001);
    assert(!late_conflict.success &&
           late_conflict.error == "manual_lease_owned_by_other_client");
    controller.maintain(session::Phase::Precheck, 35001);
    assert(controller.snapshot().state == PrecheckState::ManualAvailable);
    assert(!controller.snapshot().manual_lease);
    const auto expired_replay = controller.manualTakeover(
        "lease", "web", 3, session::Phase::Precheck, 35002);
    assert(!expired_replay.success &&
           expired_replay.error == "manual_lease_expired");
}

void test_authoritative_target_preserves_net_displacement() {
    FakeHardware hardware;
    hardware.update_measurements = false;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("net", 0));
    assert(controller.skipAuto("net", 1, 50).success);
    PrecheckStatus issued;
    assert(controller.manualTakeover("net", "web", 1,
                                     session::Phase::Precheck, 100,
                                     &issued).success);
    assert(controller.nudge(command(issued, 2), NudgeDirection::Right, 200).success);
    assert(controller.nudge(command(issued, 3), NudgeDirection::Right, 300).success);
    assert(hardware.targets[hardware.targets.size() - 2].first == 2.0);
    assert(hardware.targets.back().first == 4.0);
}

void test_fault_recovery_does_not_repeat_completed_search() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("recover", 0));
    assert(controller.skipAuto("recover", 1, 50).success);
    const auto waypoint_before = controller.snapshot().waypoint_index;
    hardware.current.state = State::Fault;
    hardware.current.fault_active = true;
    hardware.current.recoverable = true;
    hardware.current.fault_latched_reason = "STALL";
    controller.maintain(session::Phase::Precheck, 100);
    assert(controller.snapshot().state == PrecheckState::Degraded);
    controller.maintain(session::Phase::Precheck, 5100);
    assert(controller.snapshot().state == PrecheckState::ManualAvailable);
    assert(controller.snapshot().waypoint_index == waypoint_before);
    assert(controller.snapshot().search_completed);
}

void test_nonrecoverable_motion_failure_does_not_restart_search() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("stable-degraded", 0));
    hardware.current.state = State::Disabled;
    hardware.current.recoverable = false;
    hardware.current.fault_latched_reason.clear();
    hardware.current.snapshot.link_health = LinkHealth::Healthy;
    controller.maintain(session::Phase::Precheck, 100);
    assert(controller.snapshot().state == PrecheckState::Degraded);
    const auto calls_before_retry = hardware.calls.size();
    controller.maintain(session::Phase::Precheck, 6000);
    assert(controller.snapshot().state == PrecheckState::Degraded);
    assert(hardware.calls.size() == calls_before_retry);
}

void test_manual_boundary_stop_then_alternating_fine_crossing() {
    FakeHardware hardware;
    hardware.current.yaw_deg = 50.0;
    hardware.current.pitch_deg = 0.0;
    auto boundary_config = config();
    boundary_config.normal_nudge_step_deg = 30.0;
    boundary_config.fine_nudge_step_deg = 10.0;
    GimbalPrecheckController controller(hardware, boundary_config);
    assert(controller.start("boundary", 0));
    hardware.current.yaw_deg = 50.0;
    hardware.current.pitch_deg = 0.0;
    assert(controller.skipAuto("boundary", 1, 50).success);
    PrecheckStatus issued;
    assert(controller.manualTakeover("boundary", "web", 1,
                                     session::Phase::Precheck, 100,
                                     &issued).success);
    const auto stopped = controller.nudge(
        command(issued, 2), NudgeDirection::Right, 200, false);
    assert(stopped.success && stopped.boundary_stop_planned);
    assert(stopped.resolved_step_deg == 30.0);
    assert(hardware.targets.back().first == 70.0);

    const auto pending = controller.nudge(
        command(issued, 3), NudgeDirection::Right, 201, true);
    assert(!pending.success && pending.boundary_paused);
    hardware.current.snapshot.settled = true;
    controller.maintain(session::Phase::Precheck, 300);
    const auto crossed = controller.nudge(
        command(issued, 4), NudgeDirection::Right, 301, true);
    assert(crossed.success && crossed.fine_tune);
    assert(crossed.resolved_step_deg == 10.0);
    assert(hardware.targets.back().first == 80.0);
}

void test_force_release_and_session_end_origin_policy() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("release", 0));
    assert(controller.skipAuto("release", 1, 50).success);
    PrecheckStatus issued;
    assert(controller.manualTakeover("release", "web", 1,
                                     session::Phase::Precheck, 100,
                                     &issued).success);
    assert(controller.forceRelease(command(issued, 2),
                                   session::Phase::Precheck, 200).success);
    assert(controller.snapshot().state == PrecheckState::Released);
    const auto released_targets = hardware.targets.size();
    controller.stop();
    assert(hardware.targets.size() == released_targets);

    FakeHardware second_hardware;
    second_hardware.current.yaw_deg = 20.0;
    GimbalPrecheckController second(second_hardware, config());
    assert(second.start("return-origin", 0));
    second.stop();
    assert(!second_hardware.targets.empty());
    assert(second_hardware.targets.back().first == 0.0);
    assert(second_hardware.targets.back().second == 0.0);
    assert(second_hardware.calls.back() == "disable");
}

}  // namespace

int main() {
    test_board_config_is_protocol3_compact_profile();
    test_missing_persistent_calibration_enters_maintenance();
    test_precheck_holds_and_centers_before_searching();
    test_precheck_does_not_publish_searching_when_hold_fails();
    test_candidate_requires_distinct_stable_frames_and_no_implicit_lease();
    test_search_waits_for_verified_waypoints_then_returns_origin();
    test_manual_lease_owner_ttl_and_idempotency();
    test_authoritative_target_preserves_net_displacement();
    test_manual_boundary_stop_then_alternating_fine_crossing();
    test_fault_recovery_does_not_repeat_completed_search();
    test_nonrecoverable_motion_failure_does_not_restart_search();
    test_force_release_and_session_end_origin_policy();
    std::cout << "gimbal precheck tests passed\n";
    return 0;
}
