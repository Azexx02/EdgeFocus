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

SessionSnapshot SessionSnapshotPublisher::read(std::int64_t now_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto projected = snapshot_;
    projectTimingLocked(projected, now_ms);
    return projected;
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
    projectTimingLocked(snapshot_, now_ms);
}

void SessionSnapshotPublisher::projectTimingLocked(
    SessionSnapshot& snapshot, std::int64_t now_ms) const {
    std::int64_t elapsed = accumulated_focus_ms_;
    if (snapshot.phase == Phase::Focusing && focusing_started_ms_ > 0) {
        elapsed += std::max<std::int64_t>(0, now_ms - focusing_started_ms_);
    }
    snapshot.elapsed_ms = std::max<std::int64_t>(0, elapsed);
    snapshot.remaining_ms = std::max<std::int64_t>(
        0, snapshot.context.target_duration_ms - snapshot.elapsed_ms);
}

void SessionSnapshotPublisher::begin(
    const SessionContext& context,
    std::int64_t now_ms,
    std::uint64_t start_generation,
    std::string operation_id) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        const std::string boot_id = snapshot.boot_id;
        const std::uint64_t next_sequence = snapshot.sequence;
        snapshot = {};
        snapshot.boot_id = boot_id;
        snapshot.sequence = next_sequence;
        snapshot.context = context;
        snapshot.phase = Phase::Precheck;
        snapshot.runtime_active = true;
        snapshot.formal_active = false;
        snapshot.remaining_ms = context.target_duration_ms;
        snapshot.preparation.state = StartPreparationState::Initializing;
        snapshot.preparation.operation_id = std::move(operation_id);
        snapshot.preparation.generation = start_generation;
        focusing_started_ms_ = 0;
        accumulated_focus_ms_ = 0;
    });
}

void SessionSnapshotPublisher::markPreparationComplete(
    bool degraded, std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        snapshot.preparation.state = degraded
            ? StartPreparationState::Degraded
            : StartPreparationState::Ready;
        snapshot.preparation.error.clear();
        snapshot.preparation.failed_at_ms = 0;
    });
}

void SessionSnapshotPublisher::failPreparation(
    std::string error,
    std::int64_t now_ms,
    std::int64_t failed_at_wall_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        const auto preparation = snapshot.preparation;
        snapshot.phase = Phase::Idle;
        snapshot.runtime_active = false;
        snapshot.formal_active = false;
        snapshot.context = {};
        snapshot.visual = {};
        snapshot.phone = {};
        snapshot.assessment = {};
        snapshot.reminder = {};
        snapshot.gimbal = {};
        snapshot.degraded_features.clear();
        snapshot.preparation = preparation;
        snapshot.preparation.state = StartPreparationState::Failed;
        snapshot.preparation.error = std::move(error);
        snapshot.preparation.failed_at_ms = failed_at_wall_ms;
        focusing_started_ms_ = 0;
        accumulated_focus_ms_ = 0;
    });
}

void SessionSnapshotPublisher::setPhase(
    Phase phase, std::int64_t now_ms, bool formal_start) {
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
        if (formal_start && snapshot.runtime_active && phase == Phase::Focusing) {
            snapshot.formal_active = true;
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

void SessionSnapshotPublisher::updateEvidence(
    const VisualSnapshot& visual,
    const PhoneSnapshot& phone,
    const std::optional<DistractionAssessment>& assessment,
    std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        snapshot.visual = visual;
        snapshot.phone = phone;
        if (assessment) snapshot.assessment = *assessment;
    });
}

void SessionSnapshotPublisher::updateReminder(
    const ReminderSnapshot& reminder,
    std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) { snapshot.reminder = reminder; });
}

void SessionSnapshotPublisher::updateGimbal(
    const GimbalSnapshot& gimbal,
    std::int64_t now_ms) {
    GimbalUpdateCallback callback;
    SessionSnapshot published;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.gimbal = gimbal;
        updateTimingLocked(now_ms);
        ++snapshot_.sequence;
        snapshot_.snapshot_ms = now_ms;
        callback = gimbal_update_callback_;
        if (callback) published = snapshot_;
    }
    if (callback) callback(published);
}

void SessionSnapshotPublisher::setGimbalUpdateCallback(
    GimbalUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    gimbal_update_callback_ = std::move(callback);
}

void SessionSnapshotPublisher::end(std::int64_t now_ms) {
    mutate(now_ms, [&](SessionSnapshot& snapshot) {
        snapshot.phase = Phase::Idle;
        snapshot.runtime_active = false;
        snapshot.formal_active = false;
        snapshot.context = {};
        snapshot.visual = {};
        snapshot.phone = {};
        snapshot.assessment = {};
        snapshot.reminder = {};
        snapshot.gimbal = {};
        snapshot.degraded_features.clear();
        snapshot.preparation = {};
        focusing_started_ms_ = 0;
        accumulated_focus_ms_ = 0;
    });
}

}  // namespace edgefocus::session
