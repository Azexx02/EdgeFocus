#pragma once

#include "camera/CameraFrame.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace edgefocus {

struct H264PreviewPacket {
    std::string source_key;
    std::uint64_t sequence{};
    std::int64_t timestamp_ms{};
    std::int64_t encoded_at_ms{};
    int width{};
    int height{};
    bool keyframe{};
    std::vector<std::uint8_t> initialization_segment;
    std::vector<std::uint8_t> media_fragment;
};

struct H264PreviewEncoderHealth {
    bool compiled{};
    bool initialized{};
    bool healthy{};
    int consecutive_failures{};
    double encode_ms{};
    std::string error;
};

class H264PreviewEncoder {
public:
    H264PreviewEncoder();
    ~H264PreviewEncoder();

    H264PreviewEncoder(const H264PreviewEncoder&) = delete;
    H264PreviewEncoder& operator=(const H264PreviewEncoder&) = delete;

    bool encode(const CameraFrame& frame, const std::string& source_key,
                std::uint64_t sequence, H264PreviewPacket& output);
    void reset();
    void forceKeyframe();
    H264PreviewEncoderHealth health() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace edgefocus

