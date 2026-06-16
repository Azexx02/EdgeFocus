#pragma once
#include "app/Module.h"
#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "storage/ReportGenerator.h"
#include "policy/ReminderPolicy.h"
#include "policy/ModeManager.h"
#include <string>
#include <functional>
#include <mutex>
#include <vector>
#include <cstdint>

namespace edgefocus {

class AppContext;

/// BLE GATT Server for phone communication.
/// Implements Mobile API Contract v2: 10 commands + 4 push events.
/// Service UUID: 0000FF00-0000-1000-8000-00805F9B34FB
/// TX (0xFF01): phone → board JSON commands
/// RX (0xFF02): board → phone responses + push events
class BleGattServer : public Module {
public:
    BleGattServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                  ReminderPolicy& reminder_policy, ModeManager& mode_mgr,
                  ReportGenerator& report_gen);
    ~BleGattServer() override;

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    /// Handle incoming JSON command from phone, return JSON response
    std::string handleCommand(const std::string& json_cmd);

    /// Push event to connected phone (if any)
    void pushEvent(const std::string& event_type, const std::string& json_data);

    /// Check if a phone is connected
    bool isPhoneConnected() const { return phone_connected_; }

private:
    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
    ReminderPolicy& reminder_policy_;
    ModeManager& mode_mgr_;
    ReportGenerator& report_gen_;

    bool phone_connected_ = false;

    // Command handlers
    std::string handleGetDeviceStatus();
    std::string handleGetCurrentState();
    std::string handleGetTodaySummary();
    std::string handleGetLastReport();
    std::string handleSessionControl(const std::string& json_params);
    std::string handleUpdatePreferences(const std::string& json_params);
    std::string handleReminderAck(const std::string& json_params);
    std::string handlePhoneSync(const std::string& json_params);
    std::string handleGetConsoleData();

    // JSON helpers
    static std::string jsonEscape(const std::string& s);
    static std::string jsonResponse(bool ok, const std::string& data);
    static std::string getJsonString(const std::string& json, const std::string& key);
    static int getJsonInt(const std::string& json, const std::string& key, int default_val = 0);
    static bool getJsonBool(const std::string& json, const std::string& key, bool default_val = false);
};

} // namespace edgefocus
