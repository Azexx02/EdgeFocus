#include "app/AppContext.h"
#include "app/Logger.h"
#include "app/Module.h"
#include "app/BoundedQueue.h"
#include "app/SystemStateStore.h"
#include "camera/CameraPipeline.h"
#include "camera/PreviewFrameStore.h"
#include "vision/DistractionPipelineFeature.h"
#include "behavior/PhoneFactBuffer.h"
#include "session/LegacySessionProjectionFeature.h"
#include "session/SessionOrchestrator.h"
#include "session/SessionSnapshotPublisher.h"
#include "policy/ReminderPolicy.h"
#include "device/DeviceManager.h"
#include "device/GimbalMonitor.h"
#include "device/GimbalSessionFeature.h"
#include "storage/SQLiteStore.h"
#include "network/BleGattServer.h"
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
#include "network/DemoHttpServer.h"
#endif

#include <memory>
#include <chrono>
#include <thread>

using namespace edgefocus;

/// Health monitor thread. Periodically logs system health status
/// and updates SystemStateStore with device health info.
class HealthMonitor : public Module {
public:
    HealthMonitor(SystemStateStore& store) : Module("HealthMonitor"), store_(store) {}

    bool init(AppContext& /*ctx*/) override {
        LOGI("HealthMonitor", "Initialized");
        return true;
    }

    void runOnce(AppContext& ctx) override {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (ctx.running()) {
            LOGI("HealthMonitor", "focus_app is running - health OK");
        }
    }

    void shutdown(AppContext& /*ctx*/) override {
        LOGI("HealthMonitor", "Shutdown");
    }

private:
    SystemStateStore& store_;
};

int main(int argc, char* argv[]) {
    // Parse --log-level if provided
    LogLevel logLevel = LogLevel::INFO;
#ifndef EDGEFOCUS_WITH_DEMO_HTTP
    bool demo_http_flag_seen = false;
#endif
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    int demo_http_port = 0;
#endif
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-level" && i + 1 < argc) {
            std::string lv = argv[i + 1];
            if (lv == "debug") logLevel = LogLevel::DEBUG;
            else if (lv == "info")  logLevel = LogLevel::INFO;
            else if (lv == "warn")  logLevel = LogLevel::WARN;
            else if (lv == "error") logLevel = LogLevel::ERROR;
            ++i;
        } else if (arg == "--demo-http-port" && i + 1 < argc) {
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
            demo_http_port = std::stoi(argv[i + 1]);
#else
            demo_http_flag_seen = true;
#endif
            ++i;
        }
    }

    Logger::setLevel(logLevel);
    LOGI("Main", "=== EdgeFocus focus_app starting ===");
    LOGI("Main", "Log level: %d", static_cast<int>(logLevel));
#ifndef EDGEFOCUS_WITH_DEMO_HTTP
    if (demo_http_flag_seen) {
        LOGW("Main", "--demo-http-port ignored because EDGEFOCUS_WITH_DEMO_HTTP=OFF");
    }
#endif
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    if (demo_http_port > 0) {
        LOGW("Main", "Demo HTTP enabled on port %d", demo_http_port);
    }
#endif

    // Initialize SystemStateStore and SQLiteStore
    SystemStateStore store;
    auto sqlite_store_ptr = std::make_unique<SQLiteStore>();
    SQLiteStore& sqlite_store = *sqlite_store_ptr;

    // Set initial state
    DeviceStatus dev_status;
    dev_status.system_mode = SystemMode::NORMAL;
    dev_status.camera_ok = false;
    dev_status.ble_enabled = true;
    store.updateDeviceStatus(dev_status);

    // Inter-module queues (W03: vision -> behavior data contract)
    BoundedQueue<CameraFrame> camera_frame_queue(3, true);
    PreviewFrameStore preview_frames;
    session::SessionSnapshotPublisher session_snapshots("focus-app");
    PhoneFactBuffer phone_facts;
    auto gimbal_monitor_ptr = std::make_unique<GimbalMonitor>(store);
    GimbalMonitor& gimbal_monitor = *gimbal_monitor_ptr;

    session::SessionOrchestrator session_orchestrator(
        session_snapshots,
        [&](const session::SessionContext& context) {
            auto runtime = std::make_unique<session::SessionRuntime>(context);
            runtime->addFeature(
                std::make_unique<session::LegacySessionProjectionFeature>(store, sqlite_store),
                true);
            runtime->addFeature(
                std::make_unique<DistractionPipelineFeature>(
                    camera_frame_queue, phone_facts, session_snapshots, store),
                true);
            runtime->addFeature(
                std::make_unique<GimbalSessionFeature>(
                    gimbal_monitor, store, session_snapshots),
                false);
            return runtime;
        },
        [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        },
        [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        });

    AppContext ctx;

    // Register modules in dependency order
    ctx.addModule(std::make_unique<HealthMonitor>(store));
    ctx.addModule(std::make_unique<CameraPipeline>(camera_frame_queue, store, &preview_frames));
    auto device_mgr_ptr = std::make_unique<DeviceManager>(store);
    DeviceManager& device_mgr = *device_mgr_ptr;
    ctx.addModule(std::move(device_mgr_ptr));
    ctx.addModule(std::move(gimbal_monitor_ptr));
    auto reminder_policy_ptr = std::make_unique<ReminderPolicy>(store, sqlite_store, device_mgr);
    ReminderPolicy& reminder_policy = *reminder_policy_ptr;
    ctx.addModule(std::move(reminder_policy_ptr));
    ctx.addModule(std::move(sqlite_store_ptr));

    // Create BLE GATT Server
    auto ble_server = std::make_unique<BleGattServer>(store, sqlite_store, reminder_policy,
                                                       reminder_policy.getModeManager(),
                                                       &gimbal_monitor,
                                                       &session_orchestrator,
                                                       &phone_facts,
                                                       &session_snapshots);
    ctx.addModule(std::move(ble_server));

#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    if (demo_http_port > 0) {
        ctx.addModule(std::make_unique<DemoHttpServer>(store, sqlite_store, device_mgr,
                                                       gimbal_monitor, session_orchestrator,
                                                       session_snapshots, phone_facts,
                                                       preview_frames, demo_http_port));
    }
#endif

    // Board-local screen UI was removed from the product. App and demo HTTP
    // consume the session snapshot over Bluetooth/HTTP instead.

    if (!ctx.startAll()) {
        LOGE("Main", "Failed to start all modules");
        return 1;
    }

    LOGI("Main", "All modules started. Waiting for shutdown signal (Ctrl+C)...");

    // Block until SIGINT/SIGTERM or any module stops
    ctx.waitForShutdown();

    ctx.stopAll();
    LOGI("Main", "=== EdgeFocus focus_app stopped ===");
    return 0;
}
