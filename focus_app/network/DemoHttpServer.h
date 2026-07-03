#pragma once

#include "app/Module.h"
#include "app/SystemStateStore.h"
#include "device/DeviceManager.h"
#include "device/GimbalMonitor.h"
#include "storage/SQLiteStore.h"

#include <atomic>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace edgefocus {

class DemoHttpServer : public Module {
public:
    DemoHttpServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                   DeviceManager& device_mgr, GimbalMonitor& gimbal_monitor,
                   int port);

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

private:
    using json = nlohmann::json;

    void serveLoop();
    std::string handleRequest(const std::string& method, const std::string& path,
                              const std::string& body);
    json handleCommand(const json& req);
    json snapshotJson() const;
    json forceFocusState(const json& data);
    json forcePrecheckGuidance(const json& data);
    json forcePhoneState(const json& data);
    json triggerReminder(const json& data);
    json setDemoCounts(const json& data);
    json gimbalNudge(const json& data);

    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
    DeviceManager& device_mgr_;
    GimbalMonitor& gimbal_monitor_;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
};

} // namespace edgefocus
