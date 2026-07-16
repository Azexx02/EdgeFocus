#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace edgefocus {

enum class PixelFormat : uint8_t {
    BGR24 = 0,
    RGB24 = 1,
    GRAY8 = 2,
    UNKNOWN = 255
};

struct CameraFrame {
    int64_t timestamp_ms = 0;
    int64_t media_timestamp_ms = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    PixelFormat format = PixelFormat::UNKNOWN;
    std::vector<uint8_t> data;
    std::string source;

    bool valid() const {
        return width > 0
            && height > 0
            && channels > 0
            && !data.empty()
            && data.size() >= static_cast<size_t>(width * height * channels);
    }
};

} // namespace edgefocus
