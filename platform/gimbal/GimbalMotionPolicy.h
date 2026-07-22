#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace edgefocus::gimbal::motion_policy {

constexpr std::int32_t kFullTurn10 = 3600;
constexpr std::int32_t kHalfTurn10 = 1800;

inline std::int32_t normalizeAbsolute10(std::int32_t value) noexcept {
    value %= kFullTurn10;
    return value < 0 ? value + kFullTurn10 : value;
}

inline std::int32_t signedOffset10(std::int32_t absolute) noexcept {
    const auto normalized = normalizeAbsolute10(absolute);
    return normalized >= kHalfTurn10 ? normalized - kFullTurn10 : normalized;
}

inline std::int32_t shortestDelta10(std::int32_t from_absolute,
                                    std::int32_t to_absolute) noexcept {
    return signedOffset10(normalizeAbsolute10(to_absolute) -
                          normalizeAbsolute10(from_absolute));
}

enum class AxisDecision : std::uint8_t {
    Pass,
    ClampedToBoundary,
    HeldOutward,
    IntermediateTowardCenter,
};

struct AxisResult {
    std::int32_t requested10{};
    std::int32_t applied10{};
    AxisDecision decision{AxisDecision::Pass};
};

inline AxisResult resolveAutoAxis(std::int32_t current_absolute10,
                                  std::int32_t requested_absolute10,
                                  std::int32_t minimum_offset10 = -700,
                                  std::int32_t maximum_offset10 = 700) noexcept {
    const auto current = signedOffset10(current_absolute10);
    const auto requested = signedOffset10(requested_absolute10);
    AxisResult result{normalizeAbsolute10(requested_absolute10),
                      normalizeAbsolute10(requested_absolute10),
                      AxisDecision::Pass};
    if (current >= minimum_offset10 && current <= maximum_offset10) {
        const auto applied = std::clamp(requested, minimum_offset10,
                                        maximum_offset10);
        result.applied10 = normalizeAbsolute10(applied);
        if (applied != requested) result.decision = AxisDecision::ClampedToBoundary;
        return result;
    }

    const auto delta = shortestDelta10(current_absolute10, requested_absolute10);
    if (std::abs(delta) == kHalfTurn10) {
        result.applied10 = normalizeAbsolute10(
            current > maximum_offset10 ? maximum_offset10 : minimum_offset10);
        result.decision = AxisDecision::IntermediateTowardCenter;
        return result;
    }

    const bool toward_center = current > maximum_offset10
        ? delta < 0
        : delta > 0;
    if (!toward_center) {
        result.applied10 = normalizeAbsolute10(current);
        result.decision = AxisDecision::HeldOutward;
    }
    return result;
}

inline const char* axisDecisionName(AxisDecision value) noexcept {
    switch (value) {
        case AxisDecision::Pass: return "pass";
        case AxisDecision::ClampedToBoundary: return "clamped_to_boundary";
        case AxisDecision::HeldOutward: return "held_outward";
        case AxisDecision::IntermediateTowardCenter: return "intermediate_toward_center";
    }
    return "unknown";
}

}  // namespace edgefocus::gimbal::motion_policy
