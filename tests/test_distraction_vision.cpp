#include "distraction/vision.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace distraction;
    const auto meta = make_letterbox(1280, 720);
    require(meta.resized_width == 640 && meta.resized_height == 360, "16:9 resize");
    require(meta.pad_left == 0 && meta.pad_right == 0, "horizontal padding");
    require(meta.pad_top == 140 && meta.pad_bottom == 140, "vertical padding");

    std::vector<float> raw(56 * 2, 0.0F);
    auto set = [&](std::size_t channel, std::size_t candidate, float value) {
        raw[channel * 2 + candidate] = value;
    };
    for (std::size_t candidate = 0; candidate < 2; ++candidate) {
        set(0, candidate, 320); set(1, candidate, 320);
        set(2, candidate, 200); set(3, candidate, 200);
        set(4, candidate, candidate == 0 ? 0.90F : 0.80F);
        for (std::size_t point = 0; point < 17; ++point) {
            set(5 + point * 3, candidate, 320);
            set(6 + point * 3, candidate, 320);
            set(7 + point * 3, candidate, 0.9F);
        }
    }
    auto decoded = decode_yolo_pose(raw, meta, 0.35, 0.70, 300, 2);
    require(decoded.size() == 1, "NMS suppresses overlapping lower confidence box");
    require(decoded[0].raw_index == 0, "NMS preserves confidence order");

    const auto odd_meta = make_letterbox(641, 241);
    std::vector<float> odd_raw(56, 0.0F);
    odd_raw[0] = 320; odd_raw[1] = 200; odd_raw[2] = 10; odd_raw[3] = 10;
    odd_raw[4] = 0.9F;
    for (std::size_t point = 0; point < 17; ++point) {
        odd_raw[5 + point * 3] = 320;
        odd_raw[6 + point * 3] = 200;
        odd_raw[7 + point * 3] = 0.9F;
    }
    const auto odd = decode_yolo_pose(odd_raw, odd_meta, 0.35, 0.70, 300, 1);
    require(odd.size() == 1, "odd padding fixture decoded");
    require(odd[0].box.y1 == 0.0, "box uses rounded Ultralytics scale_boxes pad");
    require(odd[0].keypoints[0].y > 0.0, "keypoint uses floating Ultralytics scale_coords pad");

    auto selected = select_seat_subject(decoded, 1280, 720, {0.10, 0.05, 0.90, 0.98}, std::nullopt);
    require(selected.has_value(), "seat subject selected");
    auto features = extract_keypoint_features58(selected->detection, 1280, 720);
    require(features.valid_for_prone, "visible head and shoulders are valid");
    require(std::abs(features.values[51] - 1.0) < 1e-12, "visible ratio");

    std::cout << "vision tests passed\n";
    return 0;
}
