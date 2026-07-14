#include "camera/PreviewFrameStore.h"

namespace edgefocus {

void PreviewFrameStore::publish(const CameraFrame& frame) {
    if (!frame.valid()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = PreviewFrameSnapshot{frame, ++sequence_};
}

std::optional<PreviewFrameSnapshot> PreviewFrameStore::read() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

void PreviewFrameStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.reset();
}

}  // namespace edgefocus
