#pragma once
#include "network/AppCommandService.h"
#include "app/Module.h"
#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "policy/ReminderPolicy.h"
#include "policy/ModeManager.h"
#include <string>
#include <functional>
#include <mutex>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <atomic>

namespace edgefocus {

class AppContext;
class GimbalMonitor;
class PhoneFactBuffer;
namespace session {
class SessionOrchestrator;
class SessionSnapshotPublisher;
}

class BleGattServer : public Module {
public:
    BleGattServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                  ReminderPolicy& reminder_policy, ModeManager& mode_mgr,
                  GimbalMonitor* gimbal_monitor = nullptr,
                  session::SessionOrchestrator* session_orchestrator = nullptr,
                  PhoneFactBuffer* phone_facts = nullptr,
                  session::SessionSnapshotPublisher* session_snapshots = nullptr);
    ~BleGattServer() override;

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

    std::string handleCommand(const std::string& json_cmd);
    void pushEvent(const std::string& event_type, const std::string& json_data);
    bool isPhoneConnected() const { return phone_connected_.load(); }
    static std::vector<std::string> makeChunkPayloadsForTest(
        const std::string& payload,
        int request_id,
        bool push,
        std::size_t chunk_size);

    void setPhoneConnected(bool connected) { phone_connected_.store(connected); }

#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
    std::string bluezRxValueSnapshot();
    bool bluezRxNotifyingSnapshot();
    void bluezSetNotifying(bool notifying);
    void notifyBluez(const std::string& payload);
    void bluezOnApplicationRegistered(void* source_object, void* async_result);
    void bluezOnAdvertisementRegistered(void* source_object, void* async_result);
#endif

private:
    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
    AppCommandService app_commands_;

    std::atomic<bool> phone_connected_{false};
    bool bluez_ready_ = false;
    bool rx_notifying_ = false;
    std::string rx_value_;
    std::mutex bluez_mutex_;

#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
    void* bluez_connection_ = nullptr;
    void* object_manager_node_ = nullptr;
    void* service_node_ = nullptr;
    void* characteristic_node_ = nullptr;
    void* advertisement_node_ = nullptr;
    unsigned int app_registration_id_ = 0;
    unsigned int service_registration_id_ = 0;
    unsigned int tx_registration_id_ = 0;
    unsigned int rx_registration_id_ = 0;
    unsigned int advertisement_registration_id_ = 0;
    bool bluez_objects_registered_ = false;
    bool bluez_registering_app_ = false;
    bool bluez_app_registered_ = false;
    bool bluez_registering_advertisement_ = false;
    bool bluez_advertisement_registered_ = false;
    int64_t bluez_next_retry_ms_ = 0;

    bool initBluez();
    void shutdownBluez();
#else
    bool initBluez() { return false; }
    void shutdownBluez() {}
    void notifyBluez(const std::string&) {}
#endif
};

} // namespace edgefocus
