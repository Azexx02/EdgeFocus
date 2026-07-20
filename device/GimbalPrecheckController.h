#pragma once

#include "device/GimbalVisualTracker.h"
#include "platform/gimbal/IGimbalSessionHardware.h"
#include "session/SessionDomain.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace edgefocus::gimbal {

enum class PrecheckState : std::uint8_t {
    Inactive,
    Searching,
    Centering,
    Framing,
    Manual,
    Holding,
    Degraded,
};

enum class NudgeDirection : std::uint8_t { Left, Right, Up, Down };

struct Waypoint {
    double yaw_deg{};
    double pitch_deg{};
    std::int64_t deadline_ms{3000};
};

struct PrecheckConfig {
    std::vector<Waypoint> search_waypoints;
    std::vector<Waypoint> recovery_waypoints;
    std::optional<Waypoint> center;
    double nudge_step_deg{};
    std::int64_t initial_observe_ms{1000};
    std::int64_t waypoint_period_ms{3000};
    std::int64_t stop_queueing_ms{72000};
    std::int64_t hard_deadline_ms{75000};
    std::int64_t visual_fresh_ms{500};
    std::int64_t subject_stable_ms{400};
    unsigned subject_observations{2};
    std::int64_t candidate_centering_ms{3000};
    std::int64_t framing_window_ms{1200};
    double framing_pass_score{0.65};
    bool relative_to_alignment{};

    bool verified() const noexcept;
};

struct PrecheckStatus {
    PrecheckState state{PrecheckState::Inactive};
    std::string session_id;
    std::uint64_t generation{};
    std::size_t waypoint_index{};
    bool subject_found{};
    bool manual_lease{};
    std::uint64_t last_command_sequence{};
    std::string detail;
};

class GimbalPrecheckController {
public:
    using Clock = std::function<std::int64_t()>;

    GimbalPrecheckController(IGimbalSessionHardware& hardware,
                             PrecheckConfig config,
                             GimbalVisualTracker* centering_tracker = nullptr,
                             Clock clock = {});

    bool start(const std::string& session_id, std::int64_t now_ms, std::string* error = nullptr);
    void observe(std::int64_t now_ms, const session::VisualSnapshot& visual);
    void maintain(session::Phase phase, std::int64_t now_ms);
    CommandResult skipAuto(const std::string& session_id, std::uint64_t sequence,
                           std::int64_t now_ms);
    CommandResult nudge(const std::string& session_id, std::uint64_t sequence,
                        NudgeDirection direction, std::int64_t now_ms);
    CommandResult center(const std::string& session_id, std::uint64_t sequence,
                         std::int64_t now_ms);
    CommandResult stopMotion(const std::string& session_id, std::uint64_t sequence,
                             std::int64_t now_ms);
    CommandResult confirmPosition(const std::string& session_id, std::uint64_t sequence,
                                  std::int64_t now_ms);
    CommandResult realign(const std::string& session_id, std::uint64_t sequence,
                          std::int64_t now_ms);
    CommandResult acquireSessionManualControl(
        const std::string& session_id, std::uint64_t sequence,
        session::Phase phase, std::int64_t now_ms);
    CommandResult releaseSessionManualControl(
        const std::string& session_id, std::uint64_t sequence,
        session::Phase phase, std::int64_t now_ms);
    void holdForSession(std::int64_t now_ms);
    void stop() noexcept;
    PrecheckStatus snapshot() const;
    Status snapshotHardware() const;

private:
    bool validateManualCommandLocked(const std::string& session_id, std::uint64_t sequence,
                                     CommandResult& result);
    void finishSearchLocked(bool subject_found, std::int64_t now_ms);
    void advanceSearchLocked(std::int64_t now_ms);
    bool beginCandidateLocked(std::int64_t now_ms,
                              const session::VisualSnapshot& visual);
    void observeCenteringLocked(std::int64_t now_ms,
                                const session::VisualSnapshot& visual);
    void beginFramingLocked(std::int64_t now_ms);
    void observeFramingLocked(std::int64_t now_ms,
                              const session::VisualSnapshot& visual);
    void resumeSearchLocked(std::int64_t now_ms, std::string detail);
    bool visualInCooldownLocked(std::int64_t now_ms,
                                const session::VisualSnapshot& visual) const;
    void degradeLocked(std::string detail) noexcept;
    bool restoreLocked(session::Phase phase, std::int64_t now_ms);
    std::optional<Waypoint> resolveWaypointLocked(const Waypoint& waypoint) const;
    static CommandResult rejected(std::string error);

    IGimbalSessionHardware& hardware_;
    PrecheckConfig config_;
    GimbalVisualTracker* centering_tracker_{};
    Clock clock_;
    mutable std::mutex mutex_;
    PrecheckStatus status_;
    std::int64_t started_ms_{};
    std::int64_t next_waypoint_ms_{};
    std::int64_t first_subject_ms_{};
    std::int64_t last_subject_evidence_ms_{};
    unsigned subject_count_{};
    std::uint64_t last_command_sequence_{};
    std::int64_t next_restore_attempt_ms_{};
    std::optional<Waypoint> alignment_reference_;
    std::size_t primary_waypoint_index_{};
    std::size_t recovery_waypoint_index_{};
    std::size_t visited_waypoints_{};
    bool recovery_started_{};
    std::optional<Waypoint> active_waypoint_;
    std::optional<session::VisualBoxSnapshot> frozen_subject_box_;
    std::optional<session::VisualBoxSnapshot> cooldown_subject_box_;
    std::int64_t cooldown_until_ms_{};
    std::int64_t centering_deadline_ms_{};
    std::int64_t framing_started_ms_{};
    std::uint64_t last_visual_frame_id_{};
    unsigned missing_subject_frames_{};
    unsigned centered_frames_{};
    std::vector<double> framing_scores_;
};

const char* precheckStateName(PrecheckState state) noexcept;

}  // namespace edgefocus::gimbal
