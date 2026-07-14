#include "distraction/fusion.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <tuple>

namespace distraction {

bool TimedEvidence::fresh(std::int64_t now) const {
    return timestamp_ms <= now && now <= timestamp_ms + fresh_for_ms;
}

bool PhoneObservation::fresh(std::int64_t now) const {
    return timestamp_ms <= now && now <= timestamp_ms + fresh_for_ms;
}

namespace {

struct EpisodeUpdate {
    std::optional<EpisodeSnapshotCpp> current;
    std::optional<EpisodeSnapshotCpp> closed;
};

class TypedEpisode {
public:
    TypedEpisode(std::string kind, std::string prefix, std::int64_t enter,
                 std::int64_t exit, std::int64_t gap)
        : kind_(std::move(kind)), prefix_(std::move(prefix)), enter_(enter), exit_(exit), gap_(gap) {}

    EpisodeUpdate update(std::int64_t timestamp, std::optional<bool> state) {
        if (timestamp < 0 || (last_timestamp_ && timestamp < *last_timestamp_)) {
            throw std::invalid_argument("episode timestamps must be monotonic and non-negative");
        }
        last_timestamp_ = timestamp;
        auto gap_closed = close_for_gap(timestamp);
        if (gap_closed) {
            if (state) consume_new_segment(timestamp, *state);
            return {snapshot(), gap_closed};
        }
        if (!state) return {snapshot(), std::nullopt};
        const auto previous_valid = last_valid_;
        last_valid_ = timestamp;
        std::int64_t contiguous_delta = 0;
        if (previous_valid && timestamp - *previous_valid <= gap_) {
            contiguous_delta = timestamp - *previous_valid;
        }
        if (started_) {
            if (*state) {
                active_duration_ += contiguous_delta;
                recovery_.reset();
                return {snapshot(), std::nullopt};
            }
            if (!recovery_) recovery_ = timestamp;
            if (timestamp - *recovery_ >= exit_) {
                auto closed = close(*recovery_, "RECOVERED");
                return {std::nullopt, closed};
            }
            return {snapshot(), std::nullopt};
        }
        if (!*state) {
            candidate_.reset();
            return {};
        }
        if (!candidate_ || (contiguous_delta == 0 && previous_valid)) candidate_ = timestamp;
        if (timestamp - *candidate_ >= enter_) open(*candidate_, timestamp);
        return {snapshot(), std::nullopt};
    }

private:
    std::optional<EpisodeSnapshotCpp> snapshot() const {
        if (!started_) return std::nullopt;
        return EpisodeSnapshotCpp{id(), kind_, *started_, std::nullopt, active_duration_, std::nullopt};
    }
    std::string id() const { return prefix_ + "-" + std::to_string(counter_); }
    void open(std::int64_t start, std::int64_t now) {
        ++counter_; started_ = start; active_duration_ = now - start; candidate_.reset(); recovery_.reset();
    }
    EpisodeSnapshotCpp close(std::int64_t end, const char* reason) {
        EpisodeSnapshotCpp result{id(), kind_, *started_, end, active_duration_, std::string(reason)};
        started_.reset(); active_duration_ = 0; candidate_.reset(); recovery_.reset();
        return result;
    }
    std::optional<EpisodeSnapshotCpp> close_for_gap(std::int64_t now) {
        if (!last_valid_ || now - *last_valid_ <= gap_) return std::nullopt;
        if (started_) return close(*last_valid_, "EVIDENCE_GAP");
        candidate_.reset(); recovery_.reset(); last_valid_.reset(); return std::nullopt;
    }
    void consume_new_segment(std::int64_t timestamp, bool state) {
        last_valid_ = timestamp; candidate_ = state ? std::optional<std::int64_t>(timestamp) : std::nullopt;
        recovery_.reset();
    }
    std::string kind_;
    std::string prefix_;
    std::int64_t enter_;
    std::int64_t exit_;
    std::int64_t gap_;
    int counter_{};
    std::optional<std::int64_t> candidate_;
    std::optional<std::int64_t> recovery_;
    std::optional<std::int64_t> last_timestamp_;
    std::optional<std::int64_t> last_valid_;
    std::optional<std::int64_t> started_;
    std::int64_t active_duration_{};
};

class AggregateEpisode {
public:
    AggregateEpisode(std::int64_t exit, std::int64_t gap) : exit_(exit), gap_(gap) {}
    EpisodeUpdate update(std::int64_t timestamp, std::optional<int> level,
                         std::optional<std::int64_t> hint, bool not_present) {
        if (not_present) {
            std::optional<EpisodeSnapshotCpp> closed;
            if (started_) closed = close(timestamp, "NOT_PRESENT");
            return {std::nullopt, closed};
        }
        if (!level) {
            if (!started_) return {};
            if (!unknown_) unknown_ = timestamp;
            if (timestamp - *unknown_ > gap_) {
                const auto end = last_active_.value_or(*unknown_);
                return {std::nullopt, close(end, "EVIDENCE_GAP")};
            }
            return {snapshot(timestamp), std::nullopt};
        }
        unknown_.reset();
        if (*level > 0) {
            if (!started_) {
                ++counter_;
                started_ = std::min(hint.value_or(timestamp), timestamp);
            }
            last_active_ = timestamp; recovery_.reset();
            return {snapshot(timestamp), std::nullopt};
        }
        if (!started_) return {};
        if (!recovery_) recovery_ = timestamp;
        if (timestamp - *recovery_ >= exit_) return {std::nullopt, close(*recovery_, "RECOVERED")};
        return {snapshot(timestamp), std::nullopt};
    }
private:
    std::optional<EpisodeSnapshotCpp> snapshot(std::int64_t now) const {
        if (!started_) return std::nullopt;
        return EpisodeSnapshotCpp{"distraction-" + std::to_string(counter_), "DISTRACTION", *started_,
                                  std::nullopt, std::max<std::int64_t>(0, now - *started_), std::nullopt};
    }
    EpisodeSnapshotCpp close(std::int64_t end, const char* reason) {
        EpisodeSnapshotCpp result{"distraction-" + std::to_string(counter_), "DISTRACTION", *started_, end,
                                  std::max<std::int64_t>(0, end - *started_), std::string(reason)};
        started_.reset(); recovery_.reset(); unknown_.reset(); last_active_.reset(); return result;
    }
    std::int64_t exit_;
    std::int64_t gap_;
    int counter_{};
    std::optional<std::int64_t> started_;
    std::optional<std::int64_t> recovery_;
    std::optional<std::int64_t> unknown_;
    std::optional<std::int64_t> last_active_;
};

}  // namespace

class FusionCoreCpp::Impl {
public:
    Impl(FusionThresholds thresholds, FusionWeights weights)
        : thresholds(std::move(thresholds)), weights(std::move(weights)),
          presence_machine("NOT_PRESENT", "not-present", this->thresholds.not_present_enter_ms,
                           this->thresholds.not_present_exit_ms, this->thresholds.evidence_gap_ms),
          prone_machine("PRONE", "prone", this->thresholds.prone_enter_ms,
                        this->thresholds.prone_exit_ms, this->thresholds.evidence_gap_ms),
          aggregate(this->thresholds.distraction_exit_ms, this->thresholds.evidence_gap_ms) {}

    FusionThresholds thresholds;
    FusionWeights weights;
    TypedEpisode presence_machine;
    TypedEpisode prone_machine;
    AggregateEpisode aggregate;
    std::optional<TimedEvidence> latest_presence;
    std::optional<TimedEvidence> latest_prone;
    std::optional<PhoneObservation> latest_phone;
    std::optional<std::int64_t> phone_risk_start;
    bool phone_confirmed{};
    std::optional<PhoneCategory> latched_category;
    std::optional<std::int64_t> last_timestamp;
    std::optional<std::string> reminded_episode;

    void reset_phone() {
        phone_risk_start.reset(); phone_confirmed = false; latched_category.reset();
    }
};

FusionCoreCpp::FusionCoreCpp(FusionThresholds thresholds, FusionWeights weights)
    : impl_(new Impl(std::move(thresholds), std::move(weights))) {}
FusionCoreCpp::~FusionCoreCpp() { delete impl_; }
FusionCoreCpp::FusionCoreCpp(FusionCoreCpp&& other) noexcept : impl_(other.impl_) { other.impl_ = nullptr; }
FusionCoreCpp& FusionCoreCpp::operator=(FusionCoreCpp&& other) noexcept {
    if (this != &other) { delete impl_; impl_ = other.impl_; other.impl_ = nullptr; }
    return *this;
}

void FusionCoreCpp::reset_phone_continuity() { impl_->reset_phone(); }

void FusionCoreCpp::reset() {
    const auto thresholds = impl_->thresholds;
    const auto weights = impl_->weights;
    delete impl_;
    impl_ = new Impl(thresholds, weights);
}

FusionAssessmentCpp FusionCoreCpp::update(
    std::int64_t timestamp,
    std::optional<TimedEvidence> presence,
    std::optional<TimedEvidence> prone,
    std::optional<PhoneObservation> phone) {
    auto& state = *impl_;
    if (timestamp < 0 || (state.last_timestamp && timestamp < *state.last_timestamp)) {
        throw std::invalid_argument("fusion timestamps must be monotonic and non-negative");
    }
    state.last_timestamp = timestamp;
    if (presence && (!state.latest_presence || presence->timestamp_ms >= state.latest_presence->timestamp_ms)) {
        state.latest_presence = presence;
    }
    if (prone && (!state.latest_prone || prone->timestamp_ms >= state.latest_prone->timestamp_ms)) {
        state.latest_prone = prone;
    }
    if (phone && (!state.latest_phone || phone->timestamp_ms >= state.latest_phone->timestamp_ms)) {
        const bool previous_risk = state.latest_phone && state.latest_phone->active &&
            (state.latest_phone->category == PhoneCategory::Entertainment ||
             state.latest_phone->category == PhoneCategory::GeneralOrUnknown);
        const bool current_risk = phone->active &&
            (phone->category == PhoneCategory::Entertainment || phone->category == PhoneCategory::GeneralOrUnknown);
        if (current_risk && (!previous_risk || !state.phone_risk_start)) {
            state.phone_risk_start = phone->timestamp_ms; state.phone_confirmed = false; state.latched_category.reset();
        } else if (!current_risk) state.reset_phone();
        state.latest_phone = phone;
    }
    const auto current_evidence = [&](const std::optional<TimedEvidence>& item) -> std::optional<TimedEvidence> {
        if (!item || !item->valid || !item->value || !item->fresh(timestamp)) return std::nullopt;
        return item;
    };
    const auto current_presence = current_evidence(state.latest_presence);
    const auto current_prone = current_evidence(state.latest_prone);
    std::optional<bool> presence_signal;
    if (current_presence) {
        if (*current_presence->value >= state.weights.positive_threshold) presence_signal = true;
        else if (*current_presence->value <= 1.0 - state.weights.positive_threshold) presence_signal = false;
    }
    auto presence_update = state.presence_machine.update(
        timestamp, presence_signal ? std::optional<bool>(!*presence_signal) : std::nullopt);
    const bool confidently_present = presence_signal == true && !presence_update.current;
    std::optional<bool> prone_signal;
    if (confidently_present && current_prone) {
        prone_signal = *current_prone->value >= state.weights.positive_threshold;
    }
    auto prone_update = state.prone_machine.update(timestamp, prone_signal);

    Applicability applicability = Applicability::Unknown;
    if (presence_update.current) applicability = Applicability::NotPresent;
    else if (presence_signal == true && current_prone) applicability = Applicability::Active;

    std::optional<PhoneObservation> current_phone;
    if (state.latest_phone && state.latest_phone->fresh(timestamp)) current_phone = state.latest_phone;
    else if (state.latest_phone) state.reset_phone();
    double phone_score = 0.0;
    std::optional<std::string> phone_reason;
    if (current_phone && current_phone->active && current_phone->category) {
        if (*current_phone->category == PhoneCategory::Learning) {
            phone_score = state.weights.phone_learning_weight; phone_reason = "PHONE_LEARNING";
        } else {
            phone_reason = *current_phone->category == PhoneCategory::Entertainment
                ? "PHONE_ENTERTAINMENT" : "PHONE_GENERAL";
            const auto duration = state.phone_risk_start ? timestamp - *state.phone_risk_start : 0;
            if (!state.phone_confirmed) {
                const auto threshold = *current_phone->category == PhoneCategory::Entertainment
                    ? state.weights.level_1_after_ms : state.weights.level_2_after_ms;
                if (duration >= threshold) {
                    state.phone_confirmed = true; state.latched_category = current_phone->category;
                }
            } else if (*current_phone->category == PhoneCategory::Entertainment) {
                state.latched_category = PhoneCategory::Entertainment;
            }
            const auto effective = state.latched_category.value_or(*current_phone->category);
            phone_score = effective == PhoneCategory::Entertainment
                ? state.weights.phone_entertainment_weight : state.weights.phone_general_weight;
        }
    }

    std::vector<std::tuple<std::int64_t, double, std::string>> risks;
    std::vector<std::string> reasons;
    if (prone_update.current) {
        risks.emplace_back(prone_update.current->started_at_ms,
            std::max(current_prone ? *current_prone->value : state.weights.positive_threshold,
                     state.weights.positive_threshold), "PRONE");
        reasons.push_back("PRONE");
    }
    if (state.phone_confirmed && state.phone_risk_start) {
        risks.emplace_back(*state.phone_risk_start, phone_score, "PHONE");
    }
    if (phone_reason) reasons.push_back(*phone_reason);

    std::optional<int> level;
    if (applicability == Applicability::Active) {
        int value = 0;
        if (!risks.empty()) {
            const auto start = std::get<0>(*std::min_element(risks.begin(), risks.end(), [](const auto& a, const auto& b) {
                return std::get<0>(a) < std::get<0>(b);
            }));
            const auto duration = timestamp - start;
            if (duration >= state.weights.level_1_after_ms) value = 1;
            if (duration >= state.weights.level_2_after_ms) value = 2;
            if (risks.size() >= 2 && duration >= state.weights.multi_source_level_2_after_ms) value = std::max(value, 2);
            if (duration >= state.weights.level_3_after_ms) value = 3;
            const bool strong_visual = std::any_of(risks.begin(), risks.end(), [&](const auto& item) {
                return std::get<2>(item) != "PHONE" && std::get<1>(item) >= state.weights.strong_threshold;
            });
            if (strong_visual && state.latched_category == PhoneCategory::Entertainment &&
                duration >= state.weights.level_2_after_ms) value = 3;
        }
        level = value;
    }
    std::optional<std::int64_t> risk_start;
    for (const auto& risk : risks) risk_start = std::min(risk_start.value_or(std::get<0>(risk)), std::get<0>(risk));
    auto aggregate_update = state.aggregate.update(
        timestamp, level, risk_start, applicability == Applicability::NotPresent);
    const auto distraction = aggregate_update.closed ? aggregate_update.closed : aggregate_update.current;
    bool remind = false;
    if (applicability == Applicability::Active && level && *level >= 2 && aggregate_update.current &&
        state.reminded_episode != aggregate_update.current->id) {
        remind = true; state.reminded_episode = aggregate_update.current->id;
    }
    if (!aggregate_update.current && !aggregate_update.closed) state.reminded_episode.reset();
    return FusionAssessmentCpp{
        timestamp, applicability, level, std::move(reasons), distraction,
        prone_update.closed ? prone_update.closed : prone_update.current,
        presence_update.closed ? presence_update.closed : presence_update.current,
        remind};
}

}  // namespace distraction
