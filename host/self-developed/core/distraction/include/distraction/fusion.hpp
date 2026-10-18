#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace distraction {

enum class Applicability { Active, NotPresent, Unknown };
enum class PhoneCategory { Learning, Entertainment, GeneralOrUnknown };

struct TimedEvidence {
    std::int64_t timestamp_ms{};
    std::optional<double> value;
    bool valid{};
    std::int64_t fresh_for_ms{500};

    bool fresh(std::int64_t now_ms) const;
};

struct PhoneObservation {
    std::int64_t timestamp_ms{};
    bool active{};
    std::optional<PhoneCategory> category;
    std::int64_t fresh_for_ms{1000};

    bool fresh(std::int64_t now_ms) const;
};

struct EpisodeSnapshotCpp {
    std::string id;
    std::string kind;
    std::int64_t started_at_ms{};
    std::optional<std::int64_t> ended_at_ms;
    std::int64_t active_duration_ms{};
    std::optional<std::string> end_reason;
};

struct FusionThresholds {
    std::int64_t evidence_gap_ms{1000};
    std::int64_t not_present_enter_ms{1500};
    std::int64_t not_present_exit_ms{500};
    std::int64_t prone_enter_ms{3000};
    std::int64_t prone_exit_ms{1500};
    std::int64_t distraction_exit_ms{3000};
};

struct FusionWeights {
    double positive_threshold{0.70};
    double strong_threshold{0.85};
    std::int64_t level_1_after_ms{2000};
    std::int64_t level_2_after_ms{5000};
    std::int64_t level_3_after_ms{15000};
    std::int64_t multi_source_level_2_after_ms{2500};
    double phone_learning_weight{0.15};
    double phone_general_weight{0.45};
    double phone_entertainment_weight{0.75};
};

struct FusionAssessmentCpp {
    std::int64_t timestamp_ms{};
    Applicability applicability{Applicability::Unknown};
    std::optional<int> level;
    std::vector<std::string> reasons;
    std::optional<EpisodeSnapshotCpp> distraction_episode;
    std::optional<EpisodeSnapshotCpp> prone_episode;
    std::optional<EpisodeSnapshotCpp> presence_episode;
    bool would_remind{};
};

class FusionCoreCpp {
public:
    FusionCoreCpp(FusionThresholds thresholds = {}, FusionWeights weights = {});
    FusionAssessmentCpp update(
        std::int64_t timestamp_ms,
        std::optional<TimedEvidence> presence,
        std::optional<TimedEvidence> prone,
        std::optional<PhoneObservation> phone);
    void reset();
    void reset_phone_continuity();

private:
    class Impl;
    Impl* impl_;

public:
    ~FusionCoreCpp();
    FusionCoreCpp(const FusionCoreCpp&) = delete;
    FusionCoreCpp& operator=(const FusionCoreCpp&) = delete;
    FusionCoreCpp(FusionCoreCpp&&) noexcept;
    FusionCoreCpp& operator=(FusionCoreCpp&&) noexcept;
};

}  // namespace distraction
