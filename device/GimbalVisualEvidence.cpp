#include "device/GimbalVisualEvidence.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace edgefocus::gimbal {
namespace {

constexpr session::VisualBoxSnapshot kSeatRoi{0.10, 0.05, 0.90, 0.98};

double clamp01(double value) noexcept {
    return std::max(0.0, std::min(1.0, value));
}

double area(const session::VisualBoxSnapshot& box) noexcept {
    return std::max(0.0, box.x2 - box.x1) *
           std::max(0.0, box.y2 - box.y1);
}

double intersectionArea(const session::VisualBoxSnapshot& first,
                        const session::VisualBoxSnapshot& second) noexcept {
    return std::max(0.0, std::min(first.x2, second.x2) -
                             std::max(first.x1, second.x1)) *
           std::max(0.0, std::min(first.y2, second.y2) -
                             std::max(first.y1, second.y1));
}

double intersectionOverUnion(const session::VisualBoxSnapshot& first,
                             const session::VisualBoxSnapshot& second) noexcept {
    const double intersection = intersectionArea(first, second);
    const double united = area(first) + area(second) - intersection;
    return united > 0.0 ? intersection / united : 0.0;
}

double intersectionOverBox(const session::VisualBoxSnapshot& box,
                           const session::VisualBoxSnapshot& roi) noexcept {
    const double box_area = area(box);
    return box_area > 0.0 ? intersectionArea(box, roi) / box_area : 0.0;
}

double centerDisplacement(const session::VisualBoxSnapshot& first,
                          const session::VisualBoxSnapshot& second) noexcept {
    const double first_x = (first.x1 + first.x2) * 0.5;
    const double first_y = (first.y1 + first.y2) * 0.5;
    const double second_x = (second.x1 + second.x2) * 0.5;
    const double second_y = (second.y1 + second.y2) * 0.5;
    return std::hypot(first_x - second_x, first_y - second_y) /
           std::sqrt(2.0);
}

std::size_t visibleCount(const session::VisualSnapshot& visual,
                         std::size_t begin, std::size_t end,
                         double threshold) noexcept {
    const std::size_t bounded_end = std::min(end, visual.keypoints.size());
    std::size_t count = 0;
    for (std::size_t index = begin; index < bounded_end; ++index) {
        if (visual.keypoints[index].confidence >= threshold) ++count;
    }
    return count;
}

}  // namespace

bool isFreshVisual(const session::VisualSnapshot& visual,
                   std::int64_t now_ms,
                   std::int64_t fresh_ms) noexcept {
    return visual.frame_id != 0 && visual.evidence_timestamp_ms > 0 &&
           now_ms >= visual.evidence_timestamp_ms && fresh_ms > 0 &&
           now_ms - visual.evidence_timestamp_ms <= fresh_ms;
}

bool isPoseQuickTrigger(const session::VisualSnapshot& visual,
                        std::int64_t now_ms,
                        std::int64_t fresh_ms) noexcept {
    if (!isFreshVisual(visual, now_ms, fresh_ms) ||
        !visual.presence || *visual.presence < 0.5 ||
        !visual.subject_box || visual.subject_confidence < 0.50) {
        return false;
    }
    const auto visible = visibleCount(visual, 0, 17, 0.25);
    const auto head = visibleCount(visual, 0, 5, 0.25);
    const auto shoulders = visibleCount(visual, 5, 7, 0.25);
    return visible >= 3 && (head != 0 || shoulders != 0) &&
           intersectionOverBox(*visual.subject_box, kSeatRoi) >= 0.60;
}

VisualAssociation associateVisualSubject(
    const session::VisualSnapshot& visual,
    const session::VisualBoxSnapshot& frozen_box) {
    VisualAssociation result;
    if (!visual.subject_box || !visual.presence || *visual.presence < 0.5) {
        result.reason = "frozen_subject_not_found";
        return result;
    }
    const auto& box = *visual.subject_box;
    const double frozen_area = area(frozen_box);
    if (frozen_area <= 0.0 || area(box) <= 0.0) {
        result.reason = "invalid_association_input";
        return result;
    }
    const double iou = intersectionOverUnion(box, frozen_box);
    const double displacement = centerDisplacement(box, frozen_box);
    const double ratio = area(box) / frozen_area;
    const double seat_overlap = intersectionOverBox(box, kSeatRoi);
    const bool spatial_match =
        (iou >= 0.20 && displacement <= 0.12) || iou >= 0.35;
    if (!spatial_match || ratio < 0.55 || ratio > 1.80 ||
        seat_overlap < 0.60) {
        result.reason = "frozen_subject_not_found";
        return result;
    }
    result.accepted = true;
    result.reason = "subject_associated";
    result.subject_box = box;
    return result;
}

VisualFramingScore evaluateVisualFraming(
    const session::VisualSnapshot& visual,
    double keypoint_threshold) {
    VisualFramingScore result;
    if (!visual.subject_box) return result;
    const auto& box = *visual.subject_box;
    const double box_area = area(box);
    const double center_x = (box.x1 + box.x2) * 0.5;
    const double center_y = (box.y1 + box.y2) * 0.5;
    result.visible_keypoints = visibleCount(
        visual, 0, 17, keypoint_threshold);
    result.has_head = visibleCount(
        visual, 0, 5, keypoint_threshold) != 0;
    result.has_shoulder = visibleCount(
        visual, 5, 7, keypoint_threshold) != 0;
    result.severe_clip =
        box.x1 < 0.02 || box.y1 < 0.02 || box.x2 > 0.98;
    result.eligible = visual.subject_confidence >= 0.35 &&
                      result.visible_keypoints >= 6 && result.has_head &&
                      result.has_shoulder && box_area >= 0.08 &&
                      box_area <= 0.75 && center_x >= 0.20 &&
                      center_x <= 0.80 && center_y >= 0.20 &&
                      center_y <= 0.80;

    const double visibility = std::min(result.visible_keypoints / 10.0, 1.0);
    const double required = 0.5 * (result.has_head ? 1.0 : 0.0) +
                            0.5 * (result.has_shoulder ? 1.0 : 0.0);
    double scale = 0.0;
    if (box_area >= 0.08 && box_area <= 0.25) {
        scale = (box_area - 0.08) / (0.25 - 0.08);
    } else if (box_area > 0.25 && box_area <= 0.75) {
        scale = (0.75 - box_area) / (0.75 - 0.25);
    }
    const double center = clamp01(1.0 - std::hypot(
        (center_x - 0.5) / 0.30, (center_y - 0.5) / 0.30));
    const double clip = result.severe_clip ? 0.0 : 1.0;
    const double framing =
        0.4 * clamp01(scale) + 0.4 * center + 0.2 * clip;
    const double confidence =
        clamp01((visual.subject_confidence - 0.35) / 0.65);
    result.quality = 0.40 * visibility + 0.25 * required +
                     0.20 * framing + 0.15 * confidence;
    return result;
}

std::optional<std::pair<double, double>> visualTargetPoint(
    const session::VisualSnapshot& visual,
    double keypoint_threshold) {
    const auto visible = [&](std::size_t index) {
        return index < visual.keypoints.size() &&
               visual.keypoints[index].confidence >= keypoint_threshold &&
               std::isfinite(visual.keypoints[index].x) &&
               std::isfinite(visual.keypoints[index].y) &&
               visual.keypoints[index].x >= 0.0 &&
               visual.keypoints[index].x <= 1.0 &&
               visual.keypoints[index].y >= 0.0 &&
               visual.keypoints[index].y <= 1.0;
    };

    const bool nose = visible(0);
    const bool left_eye = visible(1);
    const bool right_eye = visible(2);
    if (nose && left_eye && right_eye) {
        return std::make_pair(
            visual.keypoints[0].x * 0.50 + visual.keypoints[1].x * 0.25 +
                visual.keypoints[2].x * 0.25,
            visual.keypoints[0].y * 0.50 + visual.keypoints[1].y * 0.25 +
                visual.keypoints[2].y * 0.25);
    }
    if (!nose && left_eye && right_eye) {
        return std::make_pair(
            (visual.keypoints[1].x + visual.keypoints[2].x) * 0.5,
            (visual.keypoints[1].y + visual.keypoints[2].y) * 0.5);
    }
    if (nose && (left_eye != right_eye)) {
        const auto& eye = visual.keypoints[left_eye ? 1 : 2];
        return std::make_pair(
            (visual.keypoints[0].x + eye.x) * 0.5,
            (visual.keypoints[0].y + eye.y) * 0.5);
    }

    double x = 0.0;
    double y = 0.0;
    std::size_t count = 0;
    for (std::size_t index = 0; index <= 4; ++index) {
        if (!visible(index)) continue;
        x += visual.keypoints[index].x;
        y += visual.keypoints[index].y;
        ++count;
    }
    if (count >= 2) {
        return std::make_pair(x / count, y / count);
    }
    if (!visual.subject_box || visual.subject_confidence < 0.50) {
        return std::nullopt;
    }
    const auto& box = *visual.subject_box;
    if (!std::isfinite(box.x1) || !std::isfinite(box.y1) ||
        !std::isfinite(box.x2) || !std::isfinite(box.y2) ||
        box.x2 <= box.x1 || box.y2 <= box.y1) {
        return std::nullopt;
    }
    const double x1 = std::clamp(box.x1, 0.0, 1.0);
    const double y1 = std::clamp(box.y1, 0.0, 1.0);
    const double x2 = std::clamp(box.x2, 0.0, 1.0);
    const double y2 = std::clamp(box.y2, 0.0, 1.0);
    if (x2 <= x1 || y2 <= y1) return std::nullopt;
    const double target_x = (x1 + x2) * 0.5;
    const double target_y = y1 + 0.25 * (y2 - y1);
    if (!std::isfinite(target_x) || !std::isfinite(target_y)) return std::nullopt;
    return std::make_pair(std::clamp(target_x, 0.0, 1.0),
                          std::clamp(target_y, 0.0, 1.0));
}

}  // namespace edgefocus::gimbal
