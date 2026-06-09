#include "app/AppContext.h"
#include "app/Logger.h"
#include "app/Module.h"
#include "app/BoundedQueue.h"
#include "app/SystemStateStore.h"
#include "camera/CameraPipeline.h"
#include "vision/VisionEngine.h"
#include "vision/VisionResult.h"
#include "behavior/BehaviorEngine.h"
#include "policy/ReminderPolicy.h"
#include "device/DeviceManager.h"
#include "storage/SQLiteStore.h"
#include "ui/UIManager.h"

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

            // Update device status in SystemStateStore
            DeviceStatus status;
            status.system_mode = SystemMode::NORMAL;  // Will be updated by ModeManager
            status.camera_ok = true;  // Will be updated by CameraPipeline
            status.bracelet_connected = false;
            status.battery_percent = -1;
            store_.updateDeviceStatus(status);
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
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-level" && i + 1 < argc) {
            std::string lv = argv[i + 1];
            if (lv == "debug") logLevel = LogLevel::DEBUG;
            else if (lv == "info")  logLevel = LogLevel::INFO;
            else if (lv == "warn")  logLevel = LogLevel::WARN;
            else if (lv == "error") logLevel = LogLevel::ERROR;
            ++i;
        }
    }

    Logger::setLevel(logLevel);
    LOGI("Main", "=== EdgeFocus focus_app starting ===");
    LOGI("Main", "Log level: %d", static_cast<int>(logLevel));

    // Initialize SystemStateStore and SQLiteStore
    SystemStateStore store;
    auto sqlite_store_ptr = std::make_unique<SQLiteStore>();
    SQLiteStore& sqlite_store = *sqlite_store_ptr;

    // Set initial state
    store.updateFocusState(FocusState::CALIBRATING, 0, "Initializing...");
    DeviceStatus dev_status;
    dev_status.system_mode = SystemMode::NORMAL;
    dev_status.camera_ok = false;
    store.updateDeviceStatus(dev_status);

    // Inter-module queues (W03: vision -> behavior data contract)
    BoundedQueue<CameraFrame> camera_frame_queue(3, true);
    BoundedQueue<VisionResult> vision_result_queue(5, false);

    AppContext ctx;

    // Register modules in dependency order
    ctx.addModule(std::make_unique<HealthMonitor>(store));
    ctx.addModule(std::make_unique<CameraPipeline>(camera_frame_queue));
    ctx.addModule(std::make_unique<VisionEngine>(vision_result_queue, camera_frame_queue));
    ctx.addModule(std::make_unique<BehaviorEngine>(vision_result_queue, store, sqlite_store));
    ctx.addModule(std::make_unique<ReminderPolicy>(store, sqlite_store));
    ctx.addModule(std::make_unique<DeviceManager>(store));
    ctx.addModule(std::move(sqlite_store_ptr));
    ctx.addModule(std::make_unique<UIManager>(store));

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
