#include "behavior/RiskScorer.h"

namespace edgefocus {

int RiskScorer::calculateScore(const FocusEvent& event) {
    int score = 0;
    if (event.is_away) score += 100;
    if (event.is_sleepy_or_slumped) score += 80;
    if (event.is_phone_in_use) {
        const int level = event.phone_distraction_level >= 1 ? event.phone_distraction_level : 1;
        score += (level == 1) ? 20 : (level == 2 ? 40 : 60);
    }
    if (event.is_looking_away) score += 40;
    
    if (score > 100) score = 100;
    return score;
}

} // namespace edgefocus
