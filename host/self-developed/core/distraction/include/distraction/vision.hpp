#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace distraction {

struct Box {
    double x1{};
    double y1{};
    double x2{};
    double y2{};
};

struct Keypoint {
    double x{};
    double y{};
    double confidence{};
};

struct Detection {
    Box box{};
    double confidence{};
    std::array<Keypoint, 17> keypoints{};
    std::size_t raw_index{};
};

struct LetterboxMeta {
    int source_width{};
    int source_height{};
    int target_width{};
    int target_height{};
    int resized_width{};
    int resized_height{};
    int pad_left{};
    int pad_right{};
    int pad_top{};
    int pad_bottom{};
    double scale{};
};

struct SeatSelection {
    Detection detection{};
    Box normalized_box{};
    std::size_t candidate_count{};
};

struct KeypointFeatures58 {
    std::array<double, 58> values{};
    std::array<bool, 17> visible{};
    bool valid_for_prone{};
};

LetterboxMeta make_letterbox(int source_width, int source_height, int target = 640);
double intersection_over_union(const Box& first, const Box& second);
double intersection_over_box(const Box& box, const Box& roi);

std::vector<Detection> decode_yolo_pose(
    const std::vector<float>& bcn_output,
    const LetterboxMeta& transform,
    double confidence_threshold = 0.35,
    double iou_threshold = 0.70,
    std::size_t max_detections = 300,
    std::size_t candidate_count = 8400);

std::optional<SeatSelection> select_seat_subject(
    const std::vector<Detection>& detections,
    int image_width,
    int image_height,
    const Box& seat_roi,
    const std::optional<Box>& previous_normalized_box);

KeypointFeatures58 extract_keypoint_features58(
    const Detection& detection,
    int image_width,
    int image_height,
    double desk_y = 0.62,
    double visibility_threshold = 0.25);

}  // namespace distraction
