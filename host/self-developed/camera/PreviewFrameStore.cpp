#include "camera/PreviewFrameStore.h"

#include <algorithm>
#include <memory>

namespace edgefocus {

void PreviewFrameStore::publish(const CameraFrame& frame) {
    if (!frame.valid()) return;
    auto immutable = std::make_shared<const CameraFrame>(frame);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (last_publish_timestamp_ms_ > 0 && frame.timestamp_ms > last_publish_timestamp_ms_) {
            const double instant_fps = 1000.0 /
                static_cast<double>(frame.timestamp_ms - last_publish_timestamp_ms_);
            publish_fps_ = publish_fps_ <= 0.0
                ? instant_fps
                : publish_fps_ * 0.8 + instant_fps * 0.2;
        }
        last_publish_timestamp_ms_ = frame.timestamp_ms;
        latest_ = PreviewFrameSnapshot{std::move(immutable), ++sequence_, publish_fps_};
    }
    changed_.notify_all();
}

std::optional<PreviewFrameSnapshot> PreviewFrameStore::read() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

std::optional<PreviewFrameSnapshot> PreviewFrameStore::readAfter(
    std::uint64_t sequence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!latest_ || latest_->sequence <= sequence) return std::nullopt;
    return latest_;
}

std::optional<PreviewFrameSnapshot> PreviewFrameStore::waitForNewer(
    std::uint64_t sequence, std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_for(lock, timeout, [&] {
        return sequence_ != sequence;
    });
    if (!latest_ || latest_->sequence <= sequence) return std::nullopt;
    return latest_;
}

PreviewFrameMetadata PreviewFrameStore::metadata() const {
    std::lock_guard<std::mutex> lock(mutex_);
    PreviewFrameMetadata value;
    value.sequence = sequence_;
    value.publish_fps = publish_fps_;
    if (!latest_ || !latest_->frame) return value;
    const auto& frame = *latest_->frame;
    value.available = true;
    value.timestamp_ms = frame.timestamp_ms;
    value.media_timestamp_ms = frame.media_timestamp_ms;
    value.width = frame.width;
    value.height = frame.height;
    return value;
}

void PreviewFrameStore::clear() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_.reset();
        ++sequence_;
        last_publish_timestamp_ms_ = 0;
        publish_fps_ = 0.0;
    }
    changed_.notify_all();
}

}  // namespace edgefocus
