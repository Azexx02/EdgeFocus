#include "behavior/DistractionAssessmentEngine.h"

#include <stdexcept>
#include <utility>

namespace edgefocus {
namespace {

session::EpisodeSnapshot map_episode(const distraction::EpisodeSnapshotCpp& value) {
    return {
        value.id,
        value.kind,
        value.started_at_ms,
        value.ended_at_ms,
        value.active_duration_ms,
        value.end_reason};
}

std::optional<session::EpisodeSnapshot> map_optional_episode(
    const std::optional<distraction::EpisodeSnapshotCpp>& value) {
    if (!value) return std::nullopt;
    return map_episode(*value);
}

}  // namespace

DistractionAssessmentEngine::DistractionAssessmentEngine(
    distraction::FusionThresholds thresholds,
    distraction::FusionWeights weights)
    : fusion_(thresholds, weights) {}

void DistractionAssessmentEngine::beginSession(std::string session_id) {
    if (session_id.empty()) throw std::invalid_argument("session_id is empty");
    session_id_ = std::move(session_id);
    fusion_.reset();
}

void DistractionAssessmentEngine::resetContinuity() {
    if (session_id_.empty()) return;
    fusion_.reset();
}

void DistractionAssessmentEngine::endSession() {
    fusion_.reset();
    session_id_.clear();
}

std::optional<distraction::TimedEvidence> DistractionAssessmentEngine::mapEvidence(
    const std::optional<EvidenceValue>& value) {
    if (!value) return std::nullopt;
    return distraction::TimedEvidence{
        value->timestamp_ms,
        value->value,
        value->valid,
        value->fresh_for_ms};
}

session::DistractionAssessment DistractionAssessmentEngine::mapAssessment(
    const distraction::FusionAssessmentCpp& value) {
    session::Applicability applicability = session::Applicability::Unknown;
    if (value.applicability == distraction::Applicability::Active) {
        applicability = session::Applicability::Active;
    } else if (value.applicability == distraction::Applicability::NotPresent) {
        applicability = session::Applicability::NotPresent;
    }
    return {
        value.timestamp_ms,
        applicability,
        value.level,
        value.reasons,
        map_optional_episode(value.distraction_episode),
        map_optional_episode(value.presence_episode),
        map_optional_episode(value.prone_episode),
        value.would_remind};
}

session::DistractionAssessment DistractionAssessmentEngine::update(
    std::int64_t now_ms,
    std::optional<EvidenceValue> presence,
    std::optional<EvidenceValue> prone,
    std::optional<distraction::PhoneObservation> phone) {
    if (session_id_.empty()) throw std::logic_error("assessment engine has no active session");
    return mapAssessment(fusion_.update(
        now_ms,
        mapEvidence(presence),
        mapEvidence(prone),
        std::move(phone)));
}

}  // namespace edgefocus
