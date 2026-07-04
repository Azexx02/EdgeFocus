#include "behavior/RiskScorer.h"
#include <algorithm>

namespace edgefocus {

namespace {
constexpr int kPhoneActiveBaseRisk = 25;

int applyPhoneUsageContext(int score, const FocusEvent& event) {
    if (!event.is_phone_in_use || score <= 0) return score;
    // phone_usage_state modulates the fused behavior risk_score. It is not a
    // CommandLevel and does not directly choose LEVEL_1/LEVEL_2/LEVEL_3.
    if (event.phone_usage_state == PhoneUsageState::PLAYING) {
        return score * 2;
    }
    if (event.phone_usage_state == PhoneUsageState::LEARNING) {
        return (score + 1) / 2;
    }
    return ((score * 3) + 1) / 2;
}
} // namespace

int RiskScorer::calculateScore(const FocusEvent& event) {
    int score = 0;
    if (event.is_away) score = std::max(score, 100);
    if (event.is_sleepy_or_slumped) score = std::max(score, 80);
    if (event.is_looking_away) score = std::max(score, 40);
    if (event.is_phone_in_use) {
        score = std::max(score, kPhoneActiveBaseRisk);
    }
    score = applyPhoneUsageContext(score, event);
    
    return std::clamp(score, 0, 100);
}

} // namespace edgefocus
