#pragma once

#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "policy/ReminderPolicy.h"
#include "policy/ModeManager.h"

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace edgefocus {

class GimbalMonitor;

class AppCommandService {
public:
    AppCommandService(SystemStateStore& store, SQLiteStore& sqlite_store,
                      ReminderPolicy& reminder_policy, ModeManager& mode_mgr,
                      GimbalMonitor* gimbal_monitor = nullptr);
    ~AppCommandService();

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
    std::string handleUploadLocalSession(const json& data, int req_id);
    std::string handleGetConsoleData(int req_id);
    std::string handleSessionFeedback(const json& data, int req_id);
    std::string handleGetSessionHistory(const json& data, int req_id);
    std::string handleGetPreferences(int req_id);
    std::string handleGetGimbalStatus(int req_id);
    std::string handleGimbalControl(const json& data, int req_id);
    std::string handleGetCameraPreviewImage(int req_id);
    std::string startGimbalTask(const std::string& action,
                                std::vector<std::pair<std::string, int>> commands,
                                int req_id);
    void joinCompletedGimbalTaskLocked();

    std::mutex gimbal_task_mutex_;
    std::thread gimbal_task_thread_;
    std::atomic<bool> gimbal_task_running_{false};
    std::atomic<int64_t> gimbal_task_seq_{0};
};

} // namespace edgefocus
