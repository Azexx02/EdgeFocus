#include "ModeManager.h"

namespace edgefocus {

ModeManager::ModeManager() : mode_(SystemMode::NORMAL) {}

void ModeManager::setMode(SystemMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = mode;
}

SystemMode ModeManager::getMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mode_;
}

} // namespace edgefocus
