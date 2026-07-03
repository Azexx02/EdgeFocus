#pragma once

#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "policy/ReminderPolicy.h"
#include "policy/ModeManager.h"

#include <nlohmann/json.hpp>
#include <string>

namespace edgefocus {

class GimbalMonitor;

class AppCommandService {
public:
    AppCommandService(SystemStateStore& store, SQLiteStore& sqlite_store,
                      ReminderPolicy& reminder_policy, ModeManager& mode_mgr,
                      GimbalMonitor* gimbal_monitor = nullptr);

    void loadPreferences();
    std::string handleCommand(const std::string& json_cmd);

    using json = nlohmann::json;

    static std::string makeResponse(int req_id, const json& data);
    static std::string makeError(int req_id, const std::string& error_msg);
    static std::string makePush(const std::string& cmd, const json& data);

private:
    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
    ReminderPolicy& reminder_policy_;
    ModeManager& mode_mgr_;
    GimbalMonitor* gimbal_monitor_;

    std::string handleGetDeviceStatus(int req_id);
    std::string handleGetCurrentState(int req_id);
    std::string handleGetTodaySummary(int req_id);
    std::string handleGetLastReport(int req_id);
    std::string handleSessionControl(const json& data, int req_id);
    std::string handleUpdatePreferences(const json& data, int req_id);
    std::string handleReminderAck(const json& data, int req_id);
    std::string handlePhoneSync(const json& data, int req_id);
    std::string handleGetConsoleData(int req_id);
    std::string handleSessionFeedback(const json& data, int req_id);
    std::string handleGetSessionHistory(const json& data, int req_id);
    std::string handleGetPreferences(int req_id);
    std::string handleGetGimbalStatus(int req_id);
    std::string handleGimbalControl(const json& data, int req_id);
    std::string handleGetCameraPreviewImage(int req_id);
};

} // namespace edgefocus
