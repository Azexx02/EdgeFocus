#pragma once

#include "camera/CameraFrame.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace edgefocus {

struct PreviewFrameSnapshot {
    std::shared_ptr<const CameraFrame> frame;
    std::uint64_t sequence{};
    double publish_fps{};
};

struct PreviewFrameMetadata {
    bool available{};
    std::uint64_t sequence{};
    std::int64_t timestamp_ms{};
    std::int64_t media_timestamp_ms{};
    int width{};
    int height{};
    double publish_fps{};
};

class PreviewFrameStore {
public:
    void publish(const CameraFrame& frame);
    std::optional<PreviewFrameSnapshot> read() const;
    std::optional<PreviewFrameSnapshot> readAfter(std::uint64_t sequence) const;
    std::optional<PreviewFrameSnapshot> waitForNewer(
        std::uint64_t sequence, std::chrono::milliseconds timeout) const;
    PreviewFrameMetadata metadata() const;
    void clear();

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::optional<PreviewFrameSnapshot> latest_;
    std::uint64_t sequence_{};
    std::int64_t last_publish_timestamp_ms_{};
    double publish_fps_{};
};

}  // namespace edgefocus
