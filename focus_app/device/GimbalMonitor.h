#pragma once

#include "app/Module.h"
#include "device/GimbalController.h"
#include <chrono>

namespace edgefocus {

class SystemStateStore;

class GimbalMonitor : public Module {
public:
    explicit GimbalMonitor(SystemStateStore& store);

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

private:
    void publishSnapshot();

    SystemStateStore& store_;
    GimbalController controller_;
    std::chrono::steady_clock::time_point last_poll_ =
        std::chrono::steady_clock::time_point{};
    static constexpr int kPollIntervalMs = 500;
};

}  // namespace edgefocus
