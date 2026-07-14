#pragma once

#include "camera/CameraFrame.h"

#include <cstdint>
#include <mutex>
#include <optional>

namespace edgefocus {

struct PreviewFrameSnapshot {
    CameraFrame frame;
    std::uint64_t sequence{};
};

class PreviewFrameStore {
public:
    void publish(const CameraFrame& frame);
    std::optional<PreviewFrameSnapshot> read() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::optional<PreviewFrameSnapshot> latest_;
    std::uint64_t sequence_{};
};

}  // namespace edgefocus
