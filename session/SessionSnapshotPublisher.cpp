#include "session/SessionSnapshotPublisher.h"

#include <algorithm>
#include <utility>

namespace edgefocus::session {

SessionSnapshotPublisher::SessionSnapshotPublisher(std::string boot_id) {
    snapshot_.boot_id = std::move(boot_id);
}

SessionSnapshot SessionSnapshotPublisher::read() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void SessionSnapshotPublisher::mutate(
    std::int64_t now_ms,
    const std::function<void(SessionSnapshot&)>& change) {
    std::lock_guard<std::mutex> lock(mutex_);
    change(snapshot_);
    updateTimingLocked(now_ms);
    ++snapshot_.sequence;
    snapshot_.snapshot_ms = now_ms;
}

void SessionSnapshotPublisher::updateTimingLocked(std::int64_t now_ms) {
    std::int64_t elapsed = accumulated_focus_ms_;
    if (snapshot_.phase == Phase::Focusing && focusing_started_ms_ > 0) {
        elapsed += std::max<std::int64_t>(0, now_ms - focusing_started_ms_);
    }
    snapshot_.elapsed_ms = std::max<std::int64_t>(0, elapsed);
    snapshot_.remaining_ms = std::max<std::int64_t>(
        0, snapshot_.context.target_duration_ms - snapshot_.elapsed_ms);
}

void SessionSnapshotPublisher::begin(const SessionContext& context, std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        const std::string boot_id = snapshot.boot_id;
        const std::uint64_t next_sequence = snapshot.sequence;
        snapshot = {};
        snapshot.boot_id = boot_id;
        snapshot.sequence = next_sequence;
        snapshot.context = context;
        snapshot.phase = Phase::Precheck;
        snapshot.runtime_active = true;
        snapshot.remaining_ms = context.target_duration_ms;
        focusing_started_ms_ = 0;
        accumulated_focus_ms_ = 0;
    });
}

void SessionSnapshotPublisher::setPhase(Phase phase, std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        const Phase previous = snapshot.phase;
        if (previous == Phase::Focusing && phase != Phase::Focusing &&
            focusing_started_ms_ > 0) {
            accumulated_focus_ms_ += std::max<std::int64_t>(0, now_ms - focusing_started_ms_);
            focusing_started_ms_ = 0;
        }
        if (phase == Phase::Focusing && previous != Phase::Focusing) {
            if (previous == Phase::Precheck) accumulated_focus_ms_ = 0;
            focusing_started_ms_ = now_ms;
        }
        snapshot.phase = phase;
    });
}

void SessionSnapshotPublisher::setDegradedFeatures(
    std::vector<std::string> features,
    std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        snapshot.degraded_features = std::move(features);
    });
}

void SessionSnapshotPublisher::updateVisual(
    const VisualSnapshot& visual,
    std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) { snapshot.visual = visual; });
}

void SessionSnapshotPublisher::updatePhone(
    const PhoneSnapshot& phone,
    std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) { snapshot.phone = phone; });
}

void SessionSnapshotPublisher::updateAssessment(
    const DistractionAssessment& assessment,
    std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) { snapshot.assessment = assessment; });
}

void SessionSnapshotPublisher::updateReminder(
    const ReminderSnapshot& reminder,
    std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) { snapshot.reminder = reminder; });
}

void SessionSnapshotPublisher::updateGimbal(
    const GimbalSnapshot& gimbal,
    std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) { snapshot.gimbal = gimbal; });
}

void SessionSnapshotPublisher::end(std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        snapshot.phase = Phase::Idle;
        snapshot.runtime_active = false;
        snapshot.context = {};
        snapshot.visual = {};
        snapshot.phone = {};
        snapshot.assessment = {};
        snapshot.reminder = {};
        snapshot.gimbal = {};
        snapshot.degraded_features.clear();
        focusing_started_ms_ = 0;
        accumulated_focus_ms_ = 0;
    });
}

}  // namespace edgefocus::session
