#include "device/GimbalPrecheckController.h"
#include "device/GimbalBoardConfig.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace edgefocus;
using namespace edgefocus::gimbal;

namespace {

class FakeHardware final : public IGimbalSessionHardware {
public:
    Status status() const override { return current; }

    CommandResult enableAndAlign(int) override {
        calls.push_back("align");
        if (!align_success) return {false, false, "align_failed"};
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
        calls.push_back("move:" + std::to_string(yaw) + "," + std::to_string(pitch));
        if (!move_success) return {false, false, "move_failed"};
        current.state = State::Holding;
        current.yaw_deg = yaw;
        current.pitch_deg = pitch;
        return {true};
    }

    CommandResult moveToVerified(double yaw, double pitch, int timeout_ms,
                                 double) override {
        return moveTo(yaw, pitch, timeout_ms);
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

    Status current{State::Ready, false, false, true, {}, 0.0, 0.0};
    bool align_success{true};
    bool move_success{true};
    bool hold_success{true};
    std::vector<std::string> calls;
};

PrecheckConfig config() {
    PrecheckConfig result;
    result.search_waypoints = {{-5.0, 0.0}, {5.0, 0.0}};
    result.center = Waypoint{0.0, 0.0};
    result.nudge_step_deg = 2.0;
    return result;
}

session::VisualSnapshot subjectAt(
    std::int64_t at_ms, double center_x = 0.5, double center_y = 0.525) {
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
                                   center_x - 0.02 +
                                       0.01 * static_cast<double>(index % 3),
                                   center_y - 0.225 +
                                       0.02 * static_cast<double>(index),
                                   0.9};
    }
    return visual;
}

TrackingConfig centeringTrackingConfig() {
    TrackingConfig result;
    result.smoothing_alpha = 1.0;
    result.minimum_command_period_ms = 100;
    return result;
}

void test_current_position_subject_causes_zero_motion() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    std::string error;
    assert(controller.start("s1", 0, &error));
    controller.observe(100, subjectAt(100));
    controller.observe(500, subjectAt(500));
    const auto state = controller.snapshot();
    assert(state.state == PrecheckState::Manual);
    assert(state.subject_found);
    assert(state.manual_lease);
    assert(hardware.calls.size() == 2);
    assert(hardware.calls.front() == "freeze");
    assert(hardware.calls.back() == "hold");
}

void test_waypoints_finish_at_center() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("s2", 0));
    controller.observe(1000, {});
    controller.observe(4000, {});
    controller.observe(7000, {});
    const auto state = controller.snapshot();
    assert(state.state == PrecheckState::Manual);
    assert(!state.subject_found);
    assert(state.manual_lease);
    assert(hardware.calls.size() == 4);
    assert(hardware.calls[0].find("move:-5.000000") == 0);
    assert(hardware.calls[1].find("move:5.000000") == 0);
    assert(hardware.calls[2].find("move:0.000000") == 0);
    assert(hardware.calls[3] == "hold");
}

void test_pose_candidate_is_centered_then_framed_before_manual_lease() {
    FakeHardware hardware;
    GimbalVisualTracker tracker(hardware, centeringTrackingConfig());
    GimbalPrecheckController controller(hardware, config(), &tracker);
    assert(controller.start("center-and-frame", 0));

    controller.observe(100, subjectAt(100, 0.65));
    assert(controller.snapshot().state == PrecheckState::Centering);
    controller.observe(200, subjectAt(200, 0.60));
    controller.observe(300, subjectAt(300));
    controller.observe(400, subjectAt(400));
    assert(controller.snapshot().state == PrecheckState::Framing);
    controller.observe(500, subjectAt(500));
    controller.observe(600, subjectAt(600));
    controller.observe(700, subjectAt(700));

    const auto state = controller.snapshot();
    assert(state.state == PrecheckState::Manual);
    assert(state.subject_found);
    assert(state.manual_lease);
    unsigned freeze_calls = 0;
    unsigned tracking_moves = 0;
    for (const auto& call : hardware.calls) {
        if (call == "freeze") ++freeze_calls;
        if (call.rfind("move:", 0) == 0) ++tracking_moves;
    }
    assert(freeze_calls == 2);
    assert(tracking_moves >= 1);
    assert(hardware.calls.back() == "hold");
}

void test_two_missing_candidate_frames_resume_waypoint_search() {
    FakeHardware hardware;
    GimbalVisualTracker tracker(hardware, centeringTrackingConfig());
    GimbalPrecheckController controller(hardware, config(), &tracker);
    assert(controller.start("candidate-lost", 0));
    controller.observe(100, subjectAt(100));
    assert(controller.snapshot().state == PrecheckState::Centering);

    session::VisualSnapshot missing;
    missing.presence = 0.0;
    missing.frame_id = 200;
    missing.evidence_timestamp_ms = 200;
    controller.observe(200, missing);
    missing.frame_id = 300;
    missing.evidence_timestamp_ms = 300;
    controller.observe(300, missing);

    const auto state = controller.snapshot();
    assert(state.state == PrecheckState::Searching);
    assert(!state.manual_lease);
    assert(state.detail == "primary_search");
}

void test_hard_deadline_centers_and_releases_manual_control() {
    FakeHardware hardware;
    GimbalVisualTracker tracker(hardware, centeringTrackingConfig());
    GimbalPrecheckController controller(hardware, config(), &tracker);
    assert(controller.start("hard-deadline", 0));
    controller.observe(75000, {});

    const auto state = controller.snapshot();
    assert(state.state == PrecheckState::Manual);
    assert(!state.subject_found);
    assert(state.manual_lease);
    assert(state.detail == "search_timeout_manual");
    assert(hardware.calls.size() == 2);
    assert(hardware.calls.front().find("move:0.000000") == 0);
    assert(hardware.calls.back() == "hold");
}

void test_production_clock_excludes_alignment_from_search_budget() {
    FakeHardware hardware;
    hardware.current.state = State::Disabled;
    std::int64_t monotonic_ms = 30000;
    GimbalVisualTracker tracker(hardware, centeringTrackingConfig());
    GimbalPrecheckController controller(
        hardware, config(), &tracker, [&] { return monotonic_ms; });
    assert(controller.start("post-align-deadline", 0));

    controller.observe(75000, {});
    assert(controller.snapshot().state == PrecheckState::Searching);
    controller.observe(105000, {});
    assert(controller.snapshot().state == PrecheckState::Manual);
    assert(controller.snapshot().detail == "search_timeout_manual");
}

void test_manual_lease_session_and_sequence_guards() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("s3", 0));
    auto skipped = controller.skipAuto("s3", 1, 50);
    assert(skipped.success);
    auto wrong = controller.nudge("wrong", 2, NudgeDirection::Left, 60);
    assert(!wrong.success && wrong.error == "session_mismatch");
    auto moved = controller.nudge("s3", 2, NudgeDirection::Right, 70);
    assert(moved.success);
    assert(hardware.current.yaw_deg == 2.0);
    auto stale = controller.nudge("s3", 2, NudgeDirection::Right, 80);
    assert(!stale.success && stale.error == "stale_command_sequence");
    auto confirmed = controller.confirmPosition("s3", 3, 90);
    assert(confirmed.success);
    assert(controller.snapshot().state == PrecheckState::Holding);
}

void test_failed_motion_degrades_and_disables() {
    FakeHardware hardware;
    hardware.move_success = false;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("s4", 0));
    controller.observe(1000, {});
    const auto state = controller.snapshot();
    assert(state.state == PrecheckState::Degraded);
    assert(!state.manual_lease);
    assert(hardware.current.state == State::Disabled);
}

void test_realign_requires_recoverable_disabled_state() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("s5", 0));
    controller.observe(100, subjectAt(100));
    controller.observe(500, subjectAt(500));
    hardware.current.state = State::Disabled;
    hardware.current.recoverable = true;
    auto realigned = controller.realign("s5", 1, 600);
    assert(realigned.success);
    assert(controller.snapshot().state == PrecheckState::Manual);
}

void test_runtime_fault_disables_use_until_passive_recovery_is_confirmed() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("s6", 0));
    controller.holdForSession(100);
    hardware.current.state = State::Fault;
    hardware.current.fault_latched_reason = "pd6_low";
    controller.maintain(session::Phase::Focusing, 200);
    assert(controller.snapshot().state == PrecheckState::Degraded);

    hardware.current.state = State::Disabled;
    hardware.current.recoverable = false;
    controller.maintain(session::Phase::Focusing, 5200);
    assert(controller.snapshot().state == PrecheckState::Degraded);

    hardware.current.recoverable = true;
    controller.maintain(session::Phase::Focusing, 10200);
    assert(controller.snapshot().state == PrecheckState::Holding);
    assert(hardware.calls[hardware.calls.size() - 2] == "align");
    assert(hardware.calls.back() == "hold");
}

void test_stop_is_idempotent_after_safe_disable() {
    FakeHardware hardware;
    GimbalPrecheckController controller(hardware, config());
    assert(controller.start("stop-once", 0));
    controller.stop();
    controller.stop();

    unsigned disable_calls = 0;
    for (const auto& call : hardware.calls) {
        if (call == "disable") ++disable_calls;
    }
    assert(disable_calls == 1);
    assert(hardware.current.state == State::Disabled);
}

}  // namespace

int main() {
    const auto board = loadGimbalBoardConfig(
        std::string(EDGEFOCUS_SOURCE_DIR) + "/config/gimbal-board.json");
    assert(board.readyForSession());
    assert(board.error.empty());
    assert(board.precheck.hard_deadline_ms == 75000);
    assert(!board.precheck.recovery_waypoints.empty());
    test_current_position_subject_causes_zero_motion();
    test_waypoints_finish_at_center();
    test_pose_candidate_is_centered_then_framed_before_manual_lease();
    test_two_missing_candidate_frames_resume_waypoint_search();
    test_hard_deadline_centers_and_releases_manual_control();
    test_production_clock_excludes_alignment_from_search_budget();
    test_manual_lease_session_and_sequence_guards();
    test_failed_motion_degrades_and_disables();
    test_realign_requires_recoverable_disabled_state();
    test_runtime_fault_disables_use_until_passive_recovery_is_confirmed();
    test_stop_is_idempotent_after_safe_disable();
    std::cout << "gimbal precheck tests passed\n";
    return 0;
}
