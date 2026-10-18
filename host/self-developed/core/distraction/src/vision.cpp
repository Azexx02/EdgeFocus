#include "distraction/vision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace distraction {
namespace {

int python_round_positive(double value) {
    const double lower = std::floor(value);
    const double fraction = value - lower;
    if (fraction < 0.5) return static_cast<int>(lower);
    if (fraction > 0.5) return static_cast<int>(lower + 1.0);
    const auto integer = static_cast<long long>(lower);
    return static_cast<int>((integer % 2 == 0) ? integer : integer + 1);
}

double clamp(double value, double lower, double upper) {
    return std::max(lower, std::min(value, upper));
}

double area(const Box& box) {
    return std::max(0.0, box.x2 - box.x1) * std::max(0.0, box.y2 - box.y1);
}

double mean_visible_y(
    const std::array<Keypoint, 17>& points,
    const std::array<bool, 17>& visible,
    const int* indices,
    std::size_t count,
    int image_height,
    bool& valid) {
    double total = 0.0;
    std::size_t used = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const int point = indices[index];
        if (visible[point]) {
            total += points[point].y / static_cast<double>(image_height);
            ++used;
        }
    }
    valid = used != 0;
    return valid ? total / static_cast<double>(used) : 0.0;
}

}  // namespace

LetterboxMeta make_letterbox(int source_width, int source_height, int target) {
    if (source_width <= 0 || source_height <= 0 || target <= 0) {
        throw std::invalid_argument("letterbox dimensions must be positive");
    }
    const double scale = std::min(
        target / static_cast<double>(source_height),
        target / static_cast<double>(source_width));
    const int resized_width = python_round_positive(source_width * scale);
    const int resized_height = python_round_positive(source_height * scale);
    const double half_width = (target - resized_width) / 2.0;
    const double half_height = (target - resized_height) / 2.0;
    const int left = python_round_positive(half_width - 0.1);
    const int right = python_round_positive(half_width + 0.1);
    const int top = python_round_positive(half_height - 0.1);
    const int bottom = python_round_positive(half_height + 0.1);
    return LetterboxMeta{
        source_width, source_height, target, target,
        resized_width, resized_height, left, right, top, bottom, scale};
}

double intersection_over_union(const Box& first, const Box& second) {
    const double intersection =
        std::max(0.0, std::min(first.x2, second.x2) - std::max(first.x1, second.x1)) *
        std::max(0.0, std::min(first.y2, second.y2) - std::max(first.y1, second.y1));
    const double united = area(first) + area(second) - intersection;
    return united <= 0.0 ? 0.0 : intersection / united;
}

double intersection_over_box(const Box& box, const Box& roi) {
    const double intersection =
        std::max(0.0, std::min(box.x2, roi.x2) - std::max(box.x1, roi.x1)) *
        std::max(0.0, std::min(box.y2, roi.y2) - std::max(box.y1, roi.y1));
    const double box_area = area(box);
    return box_area <= 0.0 ? 0.0 : intersection / box_area;
}

std::vector<Detection> decode_yolo_pose(
    const std::vector<float>& output,
    const LetterboxMeta& transform,
    double confidence_threshold,
    double iou_threshold,
    std::size_t max_detections,
    std::size_t candidate_count) {
    constexpr std::size_t channels = 56;
    if (output.size() != channels * candidate_count) {
        throw std::invalid_argument("YOLO Pose output must be [1,56,N]");
    }
    if (!(confidence_threshold >= 0.0 && confidence_threshold <= 1.0) ||
        !(iou_threshold >= 0.0 && iou_threshold <= 1.0)) {
        throw std::invalid_argument("confidence and IoU thresholds must be in [0,1]");
    }
    auto value = [&](std::size_t channel, std::size_t index) {
        const double result = output[channel * candidate_count + index];
        if (!std::isfinite(result)) throw std::invalid_argument("non-finite YOLO output");
        return result;
    };
    const double gain = std::min(
        transform.target_height / static_cast<double>(transform.source_height),
        transform.target_width / static_cast<double>(transform.source_width));
    const int box_pad_x = python_round_positive(
        (transform.target_width - transform.source_width * gain) / 2.0 - 0.1);
    const int box_pad_y = python_round_positive(
        (transform.target_height - transform.source_height * gain) / 2.0 - 0.1);
    const double keypoint_pad_x =
        (transform.target_width - transform.source_width * gain) / 2.0;
    const double keypoint_pad_y =
        (transform.target_height - transform.source_height * gain) / 2.0;

    std::vector<Detection> candidates;
    candidates.reserve(std::min<std::size_t>(candidate_count, 30000));
    for (std::size_t index = 0; index < candidate_count; ++index) {
        const double confidence = value(4, index);
        if (!(confidence > confidence_threshold)) continue;
        const double cx = value(0, index);
        const double cy = value(1, index);
        const double width = value(2, index);
        const double height = value(3, index);
        Detection detection;
        detection.confidence = confidence;
        detection.raw_index = index;
        detection.box = {
            clamp((cx - width / 2.0 - box_pad_x) / gain, 0.0, transform.source_width),
            clamp((cy - height / 2.0 - box_pad_y) / gain, 0.0, transform.source_height),
            clamp((cx + width / 2.0 - box_pad_x) / gain, 0.0, transform.source_width),
            clamp((cy + height / 2.0 - box_pad_y) / gain, 0.0, transform.source_height),
        };
        if (detection.box.x2 < detection.box.x1) std::swap(detection.box.x1, detection.box.x2);
        if (detection.box.y2 < detection.box.y1) std::swap(detection.box.y1, detection.box.y2);
        for (std::size_t point = 0; point < detection.keypoints.size(); ++point) {
            const std::size_t base = 5 + point * 3;
            detection.keypoints[point] = {
                clamp((value(base, index) - keypoint_pad_x) / gain, 0.0, transform.source_width),
                clamp((value(base + 1, index) - keypoint_pad_y) / gain, 0.0, transform.source_height),
                value(base + 2, index),
            };
        }
        candidates.push_back(detection);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Detection& a, const Detection& b) {
        if (a.confidence != b.confidence) return a.confidence > b.confidence;
        return a.raw_index < b.raw_index;
    });
    if (candidates.size() > 30000) candidates.resize(30000);

    std::vector<Detection> kept;
    kept.reserve(std::min(max_detections, candidates.size()));
    for (const auto& candidate : candidates) {
        bool suppressed = false;
        for (const auto& accepted : kept) {
            if (intersection_over_union(candidate.box, accepted.box) > iou_threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            kept.push_back(candidate);
            if (kept.size() == max_detections) break;
        }
    }
    return kept;
}

std::optional<SeatSelection> select_seat_subject(
    const std::vector<Detection>& detections,
    int image_width,
    int image_height,
    const Box& seat_roi,
    const std::optional<Box>& previous) {
    if (image_width <= 0 || image_height <= 0) {
        throw std::invalid_argument("image dimensions must be positive");
    }
    std::optional<SeatSelection> best;
    double best_score = -std::numeric_limits<double>::infinity();
    std::size_t best_index = 0;
    for (std::size_t index = 0; index < detections.size(); ++index) {
        const auto& detection = detections[index];
        Box box{
            detection.box.x1 / image_width, detection.box.y1 / image_height,
            detection.box.x2 / image_width, detection.box.y2 / image_height};
        const double cx = (box.x1 + box.x2) / 2.0;
        const double cy = (box.y1 + box.y2) / 2.0;
        const bool center_in = seat_roi.x1 <= cx && cx <= seat_roi.x2 &&
                               seat_roi.y1 <= cy && cy <= seat_roi.y2;
        const double overlap = intersection_over_box(box, seat_roi);
        if (!center_in && overlap < 0.10) continue;
        const double continuity = previous ? intersection_over_union(box, *previous) : 0.0;
        const double score = 0.55 * overlap + 0.25 * detection.confidence + 0.20 * continuity;
        if (!best || score > best_score || (score == best_score && index < best_index)) {
            best = SeatSelection{detection, box, detections.size()};
            best_score = score;
            best_index = index;
        }
    }
    return best;
}

KeypointFeatures58 extract_keypoint_features58(
    const Detection& detection,
    int image_width,
    int image_height,
    double desk_y,
    double visibility_threshold) {
    if (image_width <= 0 || image_height <= 0) {
        throw std::invalid_argument("image dimensions must be positive");
    }
    KeypointFeatures58 result;
    const double x1 = detection.box.x1 / image_width;
    const double y1 = detection.box.y1 / image_height;
    const double x2 = detection.box.x2 / image_width;
    const double y2 = detection.box.y2 / image_height;
    const double width = std::max(x2 - x1, 1e-6);
    const double height = std::max(y2 - y1, 1e-6);
    std::size_t visible_count = 0;
    for (std::size_t index = 0; index < 17; ++index) {
        const auto& point = detection.keypoints[index];
        const bool visible = point.confidence >= visibility_threshold;
        result.visible[index] = visible;
        if (visible) ++visible_count;
        result.values[index * 2] = visible
            ? clamp((point.x / image_width - x1) / width, 0.0, 1.0) : 0.0;
        result.values[index * 2 + 1] = visible
            ? clamp((point.y / image_height - y1) / height, 0.0, 1.0) : 0.0;
        result.values[34 + index] = visible ? 1.0 : 0.0;
    }
    const int head_indices[] = {0, 1, 2, 3, 4};
    const int shoulder_indices[] = {5, 6};
    const int hip_indices[] = {11, 12};
    bool has_head = false;
    bool has_shoulders = false;
    bool has_hips = false;
    const double head_y = mean_visible_y(detection.keypoints, result.visible, head_indices, 5, image_height, has_head);
    const double shoulder_y = mean_visible_y(detection.keypoints, result.visible, shoulder_indices, 2, image_height, has_shoulders);
    const double hip_y = mean_visible_y(detection.keypoints, result.visible, hip_indices, 2, image_height, has_hips);
    result.values[51] = visible_count / 17.0;
    result.values[52] = head_y;
    result.values[53] = shoulder_y;
    result.values[54] = hip_y;
    result.values[55] = has_head ? head_y - desk_y : 0.0;
    result.values[56] = has_shoulders ? shoulder_y - desk_y : 0.0;
    result.values[57] = has_shoulders && has_hips ? hip_y - shoulder_y : 0.0;
    result.valid_for_prone = has_head && has_shoulders;
    return result;
}

}  // namespace distraction
