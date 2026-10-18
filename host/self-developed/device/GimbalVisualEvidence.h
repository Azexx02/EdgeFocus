#pragma once

#include "session/SessionDomain.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace edgefocus::gimbal {

struct VisualAssociation {
    bool accepted{};
    std::string reason;
    std::optional<session::VisualBoxSnapshot> subject_box;
};

struct VisualFramingScore {
    bool eligible{};
    double quality{};
    std::size_t visible_keypoints{};
    bool has_head{};
    bool has_shoulder{};
    bool severe_clip{};
};

bool isFreshVisual(const session::VisualSnapshot& visual,
                   std::int64_t now_ms,
                   std::int64_t fresh_ms) noexcept;
bool isPoseQuickTrigger(const session::VisualSnapshot& visual,
                        std::int64_t now_ms,
                        std::int64_t fresh_ms) noexcept;
VisualAssociation associateVisualSubject(
    const session::VisualSnapshot& visual,
    const session::VisualBoxSnapshot& frozen_box);
VisualFramingScore evaluateVisualFraming(
    const session::VisualSnapshot& visual,
    double keypoint_threshold = 0.25);
std::optional<std::pair<double, double>> visualTargetPoint(
    const session::VisualSnapshot& visual,
    double keypoint_threshold = 0.25);

}  // namespace edgefocus::gimbal
