#include "device/GimbalVisualTracker.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace edgefocus::gimbal;
namespace session = edgefocus::session;

namespace {

class FakeHardware final : public IGimbalSessionHardware {
public:
    Status status() const override { return current; }
    CommandResult enableAndAlign(int) override { return {true}; }
    CommandResult stopAndDisable(int) override { return {true}; }
    CommandResult moveTo(double, double, int) override { return {true}; }
    CommandResult moveToVerified(double yaw, double pitch, int timeout_ms,
                                 double) override {
        return trackTo(yaw, pitch, timeout_ms);
    }
    CommandResult moveToManualVerified(double yaw, double pitch, int timeout_ms,
                                       double) override {
        return trackTo(yaw, pitch, timeout_ms);
    }
    CommandResult rebaseAlignmentAtCurrent(int) override { return {true}; }
    CommandResult trackTo(double yaw, double pitch, int) override {
        targets.push_back({yaw, pitch});
        current.yaw_deg = yaw;
        current.pitch_deg = pitch;
        return track_success ? CommandResult{true}
                             : CommandResult{false, false, "track_failed"};
    }
    CommandResult hold(int) override { return {true}; }
    CommandResult refreshAngle(int) override { return {true}; }
    CommandResult freezeAtCurrent(int) override { return {true}; }
    CommandResult recover(int) override { return {true}; }

    Status current{State::Holding, false, false, true, {}, 0.0, 0.0};
    bool track_success{true};
    std::vector<std::pair<double, double>> targets;
};

session::VisualSnapshot visual(std::uint64_t frame, std::int64_t at, double x, double y) {
    session::VisualSnapshot value;
    value.frame_id = frame;
    value.evidence_timestamp_ms = at;
    value.presence = 1.0;
    value.subject_confidence = 0.9;
    value.subject_box = session::VisualBoxSnapshot{x - 0.1, y - 0.1, x + 0.1, y + 0.3};
    return value;
}

}  // namespace

int main() {
    FakeHardware hardware;
    TrackingConfig config;
    config.smoothing_alpha = 1.0;
    GimbalVisualTracker tracker(hardware, config);

    auto result = tracker.update(1000, visual(1, 1000, 0.8, 0.35));
    assert(result.success);
    assert(hardware.targets.size() == 1);
    assert(hardware.targets.back().first > 0.0);
    assert(tracker.status().tracking);

    result = tracker.update(1050, visual(2, 1050, 0.8, 0.35));
    assert(result.success && hardware.targets.size() == 1);
    result = tracker.update(1200, visual(3, 1200, 0.5, 0.35));
    assert(result.success);

    session::VisualSnapshot missing;
    missing.frame_id = 4;
    missing.evidence_timestamp_ms = 3000;
    missing.presence = 0.0;
    assert(tracker.update(3000, missing).success);
    assert(!tracker.status().tracking);

    std::cout << "gimbal visual tracking tests passed\n";
    return 0;
}
