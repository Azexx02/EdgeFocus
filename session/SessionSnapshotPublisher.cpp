#include "session/SessionSnapshotPublisher.h"

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
    ++snapshot_.sequence;
    snapshot_.snapshot_ms = now_ms;
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
    });
}

void SessionSnapshotPublisher::setPhase(Phase phase, std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) { snapshot.phase = phase; });
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
        snapshot.visual = {};
        snapshot.phone = {};
        snapshot.assessment = {};
        snapshot.reminder = {};
        snapshot.gimbal = {};
        snapshot.degraded_features.clear();
    });
}

}  // namespace edgefocus::session
