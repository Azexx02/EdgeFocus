#pragma once
#include "app/Module.h"
#include "behavior/FocusState.h"
#include "policy/NotificationRouter.h"
#include "InterventionCommand.h"
#include "ModeManager.h"
#include <optional>
#include <cstdint>

#include "storage/SQLiteStore.h"

namespace edgefocus {

class AppContext;
class SystemStateStore;
class DeviceManager;

/// Reminder policy module. Generates InterventionCommands based on FocusState.
class ReminderPolicy : public Module {
public:
    ReminderPolicy(SystemStateStore& store, SQLiteStore& sqlite_store, DeviceManager& device_mgr);
    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    std::optional<InterventionCommand> processState(FocusState state, int64_t now_ms);
    void onUserAck(int64_t now_ms);

    ModeManager& getModeManager() { return mode_manager_; }

private:
    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
    DeviceManager& device_mgr_;
    NotificationRouter notifier_;
    ModeManager mode_manager_;
    int64_t last_reminder_time_ms_{0};
    int64_t base_cooling_ms_{5000};
    int64_t current_cooling_ms_{5000};
    CommandLevel last_level_{CommandLevel::NONE};
};

} // namespace edgefocus
