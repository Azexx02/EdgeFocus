#pragma once

#include "session/SessionDomain.h"

#include <functional>
#include <mutex>
#include <string>

namespace edgefocus::session {

class SessionSnapshotPublisher {
public:
    explicit SessionSnapshotPublisher(std::string boot_id = {});

    SessionSnapshot read() const;
    void begin(const SessionContext& context, std::int64_t now_ms,
               std::uint64_t start_generation = 0,
               std::string operation_id = {});
    void markPreparationComplete(bool degraded, std::int64_t now_ms);
    void failPreparation(std::string error, std::int64_t now_ms,
                         std::int64_t failed_at_wall_ms);
    void setPhase(Phase phase, std::int64_t now_ms, bool formal_start = false);
    void setDegradedFeatures(std::vector<std::string> features, std::int64_t now_ms);
    void updateVisual(const VisualSnapshot& visual, std::int64_t now_ms);
    void updatePhone(const PhoneSnapshot& phone, std::int64_t now_ms);
    void updateAssessment(const DistractionAssessment& assessment, std::int64_t now_ms);
    void updateEvidence(const VisualSnapshot& visual,
                        const PhoneSnapshot& phone,
                        const std::optional<DistractionAssessment>& assessment,
                        std::int64_t now_ms);
    void updateReminder(const ReminderSnapshot& reminder, std::int64_t now_ms);
    void updateGimbal(const GimbalSnapshot& gimbal, std::int64_t now_ms);
    void end(std::int64_t now_ms);

private:
    void mutate(std::int64_t now_ms, const std::function<void(SessionSnapshot&)>& change);
    void updateTimingLocked(std::int64_t now_ms);

    mutable std::mutex mutex_;
    SessionSnapshot snapshot_;
    std::int64_t focusing_started_ms_{};
    std::int64_t accumulated_focus_ms_{};
};

}  // namespace edgefocus::session
