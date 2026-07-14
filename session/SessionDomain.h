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

struct SessionContext {
    std::string session_id;
    SourceMode source_mode{SourceMode::LiveCamera};
    std::int64_t started_monotonic_ms{};
    std::int64_t started_wall_ms{};
    std::int64_t target_duration_ms{};
};

struct PhoneFact {
    std::string session_id;
    std::uint64_t sequence{};
    std::int64_t received_monotonic_ms{};
    std::int64_t source_timestamp_ms{};
    bool active{};
    std::optional<PhoneCategory> category;
    std::int64_t fresh_for_ms{1000};
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

struct VisualSnapshot {
    std::optional<double> presence;
    std::optional<double> prone;
    std::int64_t evidence_timestamp_ms{};
    std::uint64_t frame_id{};
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
    FeatureHealth health;
    double angle_x{};
    double angle_y{};
    bool tracking{};
    bool manual_lease{};
};

struct SessionSnapshot {
    std::string schema_version{"2.0"};
    std::string boot_id;
    std::uint64_t sequence{};
    std::int64_t snapshot_ms{};
    SessionContext context;
    Phase phase{Phase::Idle};
    bool runtime_active{};
    std::int64_t elapsed_ms{};
    std::int64_t remaining_ms{};
    VisualSnapshot visual;
    PhoneSnapshot phone;
    DistractionAssessment assessment;
    ReminderSnapshot reminder;
    GimbalSnapshot gimbal;
    std::vector<std::string> degraded_features;
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

}  // namespace edgefocus::session
