#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace edgefocus::session {

enum class Phase : std::uint8_t {
    Idle,
    Precheck,
    Focusing,
    Paused,
    Ending,
};

enum class SourceMode : std::uint8_t {
    LiveCamera,
    RecordedInput,
    EvidenceReplay,
};

enum class Applicability : std::uint8_t {
    Active,
    NotPresent,
    Unknown,
};

enum class PhoneCategory : std::uint8_t {
    Learning,
    Entertainment,
    GeneralOrUnknown,
};

enum class StopReason : std::uint8_t {
    Completed,
    Cancelled,
    Interrupted,
    PrepareFailed,
    ProcessShutdown,
};

enum class StartPreparationState : std::uint8_t {
    None,
    Initializing,
    Ready,
    Degraded,
    Failed,
};

struct SessionContext {
    std::string session_id;
    SourceMode source_mode{SourceMode::LiveCamera};
    std::int64_t started_monotonic_ms{};
    std::int64_t started_wall_ms{};
    std::int64_t target_duration_ms{};
};

struct FormalStartContext {
    std::int64_t started_monotonic_ms{};
    std::int64_t started_wall_ms{};
};

struct PhoneFact {
    std::string session_id;
    std::uint64_t sequence{};
    std::int64_t received_monotonic_ms{};
    std::int64_t source_timestamp_ms{};
    bool active{};
    std::optional<PhoneCategory> category;
    std::int64_t fresh_for_ms{5000};
};

struct EpisodeSnapshot {
    std::string id;
    std::string kind;
    std::int64_t started_at_ms{};
    std::optional<std::int64_t> ended_at_ms;
    std::int64_t active_duration_ms{};
    std::optional<std::string> end_reason;
};

struct DistractionAssessment {
    std::int64_t timestamp_ms{};
    Applicability applicability{Applicability::Unknown};
    std::optional<int> level;
    std::vector<std::string> reasons;
    std::optional<EpisodeSnapshot> distraction_episode;
    std::optional<EpisodeSnapshot> presence_episode;
    std::optional<EpisodeSnapshot> prone_episode;
    bool would_remind{};
};

struct FeatureHealth {
    bool available{};
    bool degraded{};
    std::string status{"not_started"};
    std::string detail;
    std::int64_t updated_at_ms{};
};

struct VisualBoxSnapshot {
    double x1{};
    double y1{};
    double x2{};
    double y2{};
};

struct VisualKeypointSnapshot {
    double x{};
    double y{};
    double confidence{};
};

struct VisualSnapshot {
    std::optional<double> presence;
    std::optional<double> prone;
    std::int64_t evidence_timestamp_ms{};
    std::uint64_t frame_id{};
    std::int64_t media_timestamp_ms{};
    std::string source;
    std::optional<VisualBoxSnapshot> subject_box;
    std::vector<VisualKeypointSnapshot> keypoints;
    double subject_confidence{};
    double inference_fps{};
    double inference_latency_ms{};
};

struct PhoneSnapshot {
    bool fresh{};
    bool active{};
    std::optional<PhoneCategory> category;
    std::uint64_t sequence{};
    std::int64_t timestamp_ms{};
};

struct ReminderSnapshot {
    std::string event_id;
    std::string episode_id;
    std::optional<int> level;
    std::string state{"idle"};
    std::vector<std::string> delivered_to;
    bool acknowledged{};
};

struct GimbalSnapshot {
    std::string state{"idle"};
    std::string protocol_version;
    std::vector<std::string> capabilities;
    bool available{};
    std::string degraded_reason;
    std::string link_health{"unavailable"};
    std::string control_mode{"uart_only"};
    FeatureHealth health;
    std::optional<double> angle_x;
    std::optional<double> angle_y;
    std::optional<std::int32_t> current_yaw10;
    std::optional<std::int32_t> current_pitch10;
    std::optional<std::int32_t> target_yaw10;
    std::optional<std::int32_t> target_pitch10;
    std::optional<std::uint32_t> motion_generation;
    bool settled{};
    bool tracking{};
    bool manual_lease{};
    std::string lease_owner_id;
    std::uint64_t lease_epoch{};
    std::int64_t lease_expires_at_ms{};
    std::uint64_t search_generation{};
    std::uint32_t waypoint_index{};
    bool subject_found{};
    bool search_completed{};
    bool calibration_valid{};
    std::uint32_t calibration_epoch{};
    bool origin_valid{};
    std::uint32_t origin_epoch{};
    bool recoverable{};
    std::string fault_class{"none"};
    std::string fault_code;
    std::string fault_latched_reason;
    std::int64_t last_pose_age_ms{-1};
    std::uint64_t last_queued_sequence{};
    std::uint64_t acked_through_sequence{};
    std::string current_protection_state;
    std::string detail;
};

struct StartPreparationSnapshot {
    StartPreparationState state{StartPreparationState::None};
    std::string operation_id;
    std::uint64_t generation{};
    std::string error;
    std::int64_t failed_at_ms{};
};

struct SessionSnapshot {
    std::string schema_version{"2.0"};
    std::string boot_id;
    std::uint64_t sequence{};
    std::int64_t snapshot_ms{};
    SessionContext context;
    Phase phase{Phase::Idle};
    bool runtime_active{};
    bool formal_active{};
    std::int64_t elapsed_ms{};
    std::int64_t remaining_ms{};
    VisualSnapshot visual;
    PhoneSnapshot phone;
    DistractionAssessment assessment;
    ReminderSnapshot reminder;
    GimbalSnapshot gimbal;
    std::vector<std::string> degraded_features;
    StartPreparationSnapshot preparation;
};

inline const char* phase_name(Phase value) {
    switch (value) {
        case Phase::Idle: return "IDLE";
        case Phase::Precheck: return "PRECHECK";
        case Phase::Focusing: return "FOCUSING";
        case Phase::Paused: return "PAUSED";
        case Phase::Ending: return "ENDING";
    }
    return "IDLE";
}

inline const char* preparation_state_name(StartPreparationState value) {
    switch (value) {
        case StartPreparationState::None: return "NONE";
        case StartPreparationState::Initializing: return "INITIALIZING";
        case StartPreparationState::Ready: return "READY";
        case StartPreparationState::Degraded: return "DEGRADED";
        case StartPreparationState::Failed: return "FAILED";
    }
    return "NONE";
}

}  // namespace edgefocus::session
