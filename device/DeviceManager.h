#pragma once
#include "app/Module.h"
#include "app/BoundedQueue.h"
#include "policy/NotificationRouter.h"
#include "policy/InterventionCommand.h"
#include "device/ScreenController.h"
#include "device/BleBandController.h"
#include "device/SoundController.h"
#include <array>
#include <cstdint>

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
    bool issueAction(const DeviceAction& action);
    int executedCommandCount() const { return executed_command_count_; }
    CommandLevel lastExecutedLevel() const { return last_executed_level_; }
    int screenFlashCount() const { return screen_ctrl_.flashCount(); }
    uint8_t lastBandAlertPattern() const { return ble_ctrl_.lastAlertPattern(); }
    uint8_t lastBandAlertLevel() const { return ble_ctrl_.lastAlertLevel(); }
    std::array<uint8_t, 3> lastBandAlertFrame() const { return ble_ctrl_.lastAlertFrame(); }
    int soundPlayCount() const { return sound_ctrl_.playCount(); }
    bool requestBandConnection(bool reconnect) { return ble_ctrl_.requestImmediateConnection(reconnect); }
    bool isBandConnected() const { return ble_ctrl_.isConnected(); }

private:
    void executeCommand(const InterventionCommand& cmd);
    void executeAction(const DeviceAction& action);

    BoundedQueue<InterventionCommand> command_queue_{20};
    BoundedQueue<DeviceAction> action_queue_{20};
    ScreenController screen_ctrl_;
    BleBandController ble_ctrl_;
    SoundController sound_ctrl_;
    SystemStateStore& store_;
    int executed_command_count_ = 0;
    CommandLevel last_executed_level_ = CommandLevel::NONE;
    std::uint64_t last_band_sent_sequence_ = 0;
};

} // namespace edgefocus
