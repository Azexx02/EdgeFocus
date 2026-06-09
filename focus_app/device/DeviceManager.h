#pragma once
#include "app/Module.h"
#include "app/BoundedQueue.h"
#include "policy/InterventionCommand.h"
#include "device/ScreenController.h"
#include "device/BleBandController.h"

namespace edgefocus {

class AppContext;
class SystemStateStore;

/// Device abstraction module. Executes InterventionCommands on peripherals.
/// W07 will implement real device control.
class DeviceManager : public Module {
public:
    DeviceManager(SystemStateStore& store);
    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    bool issueCommand(const InterventionCommand& cmd);

private:
    BoundedQueue<InterventionCommand> command_queue_{20};
    ScreenController screen_ctrl_;
    BleBandController ble_ctrl_;
    SystemStateStore& store_;
};

} // namespace edgefocus
