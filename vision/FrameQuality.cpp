#include "FrameQuality.h"

#include <algorithm>
#include <cmath>

namespace edgefocus {

namespace {

float sampleLuma(const CameraFrame& frame, int x, int y) {
    const size_t idx = static_cast<size_t>((y * frame.width + x) * frame.channels);
    if (frame.channels == 1) {
        return static_cast<float>(frame.data[idx]);
    }
    const float b = static_cast<float>(frame.data[idx + 0]);
    const float g = static_cast<float>(frame.data[idx + 1]);
    const float r = static_cast<float>(frame.data[idx + 2]);
    return 0.114f * b + 0.587f * g + 0.299f * r;
}

} // namespace

FrameQuality FrameQualityEvaluator::evaluate(const CameraFrame& frame, FrameQualityMetrics* metrics) const {
    FrameQuality quality;
    quality.valid = false;
    quality.level = FrameQualityLevel::UNKNOWN;
    quality.score = 0.0f;
    quality.reason = "frame unavailable";

    if (!frame.valid()) {
        return quality;
    }

    const int step_x = std::max(1, frame.width / 64);
    const int step_y = std::max(1, frame.height / 48);
    double sum = 0.0;
    double sum_sq = 0.0;
    double edge_sum = 0.0;
    int count = 0;
    int edge_count = 0;

    for (int y = 0; y < frame.height; y += step_y) {
        for (int x = 0; x < frame.width; x += step_x) {
            const float luma = sampleLuma(frame, x, y);
            sum += luma;
            sum_sq += luma * luma;
            ++count;

            if (x + step_x < frame.width && y + step_y < frame.height) {
                const float dx = std::fabs(luma - sampleLuma(frame, x + step_x, y));
                const float dy = std::fabs(luma - sampleLuma(frame, x, y + step_y));
                edge_sum += dx + dy;
                ++edge_count;
            }
        }
    }

    if (count == 0) {
        return quality;
    }

    const float mean = static_cast<float>(sum / count);
    const float variance = static_cast<float>(std::max(0.0, (sum_sq / count) - (mean * mean)));
    const float contrast = std::sqrt(variance);
    const float edge_score = edge_count > 0 ? static_cast<float>(edge_sum / edge_count) : 0.0f;

    if (metrics) {
        metrics->mean_luma = mean;
        metrics->contrast = contrast;
        metrics->edge_score = edge_score;
    }

    quality.valid = true;
    if (mean < 35.0f) {
        quality.level = FrameQualityLevel::DARK;
        quality.score = std::max(0.05f, mean / 70.0f);
        quality.reason = "frame too dark";
    } else if (mean > 225.0f) {
        quality.level = FrameQualityLevel::BRIGHT;
        quality.score = std::max(0.05f, (255.0f - mean) / 60.0f);
        quality.reason = "frame too bright";
    } else if (edge_score < 4.0f && contrast < 18.0f) {
        quality.level = FrameQualityLevel::BLURRY;
        quality.score = 0.35f;
        quality.reason = "low edge detail";
    } else {
        quality.level = FrameQualityLevel::GOOD;
        const float exposure_score = 1.0f - std::min(1.0f, std::fabs(mean - 128.0f) / 128.0f);
        const float contrast_score = std::min(1.0f, contrast / 55.0f);
        const float detail_score = std::min(1.0f, edge_score / 24.0f);
        quality.score = std::clamp(0.35f + 0.30f * exposure_score + 0.20f * contrast_score + 0.15f * detail_score,
                                   0.0f, 1.0f);
        quality.reason = "quality ok";
    }

    return quality;
}

} // namespace edgefocus
