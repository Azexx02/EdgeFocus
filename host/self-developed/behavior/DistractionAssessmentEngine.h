#pragma once

#include "behavior/PhoneFactBuffer.h"
#include "session/SessionDomain.h"
#include "distraction/fusion.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace edgefocus {

struct EvidenceValue {
    std::int64_t timestamp_ms{};
    std::optional<double> value;
    bool valid{};
    std::int64_t fresh_for_ms{500};
};

class DistractionAssessmentEngine {
public:
    DistractionAssessmentEngine(
        distraction::FusionThresholds thresholds = {},
        distraction::FusionWeights weights = {});

    void beginSession(std::string session_id);
    void resetContinuity();
    void endSession();

    session::DistractionAssessment update(
        std::int64_t now_ms,
        std::optional<EvidenceValue> presence,
        std::optional<EvidenceValue> prone,
        std::optional<distraction::PhoneObservation> phone);

    const std::string& sessionId() const noexcept { return session_id_; }

private:
    static std::optional<distraction::TimedEvidence> mapEvidence(
        const std::optional<EvidenceValue>& value);
    static session::DistractionAssessment mapAssessment(
        const distraction::FusionAssessmentCpp& assessment);

    distraction::FusionCoreCpp fusion_;
    std::string session_id_;
};

}  // namespace edgefocus
