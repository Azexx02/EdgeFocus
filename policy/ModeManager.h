#pragma once
#include <mutex>
#include <cstdint>

namespace edgefocus {

enum class SystemMode : uint8_t {
    NORMAL = 0,
    SILENT = 1,
    HOME = 2
};

enum class NotificationMode : uint8_t {
    NORMAL = 0,
    SILENT = 1
};

enum class PowerProfile : uint8_t {
    HOME = 0,
    GO = 1
};

class ModeManager {
public:
    ModeManager();

    void setMode(SystemMode mode);
    SystemMode getMode() const;

private:
    mutable std::mutex mutex_;
    SystemMode mode_;
};

} // namespace edgefocus
