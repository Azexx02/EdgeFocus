#include "app/AppContext.h"
#include "app/Logger.h"
#include "app/Module.h"
#include "app/BoundedQueue.h"
#include "app/SystemStateStore.h"
#include "camera/CameraPipeline.h"
#include "camera/PreviewFrameStore.h"
#include "vision/DistractionPipelineFeature.h"
#include "demo/BoardDemoRuntime.h"
#include "behavior/PhoneFactBuffer.h"
#include "session/LegacySessionProjectionFeature.h"
#include "session/SessionOrchestrator.h"
#include "session/SessionSnapshotPublisher.h"
#include "policy/ReminderPolicy.h"
#include "device/DeviceManager.h"
#include "storage/SQLiteStore.h"
#include "network/BleGattServer.h"
#include "platform/power/LinuxI2cPowerSourceIo.h"
#include "platform/power/PowerSourceService.h"
#include "platform/power/WorkloadProfilePolicy.h"
#include "platform/gimbal/GimbalHardwareService.h"
#include "platform/gimbal/LinuxGimbalIo.h"
#include "device/GimbalBoardConfig.h"
#include "device/GimbalSessionFeature.h"
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
#include "network/DemoHttpServer.h"
#endif

#include <memory>
#include <algorithm>
#include <chrono>
#include <cstdlib>
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
    power::WorkloadProfilePolicy workload_policy;
    std::unique_ptr<gimbal::GimbalHardwareService> gimbal_service_ptr;
    std::unique_ptr<gimbal::GimbalPrecheckController> gimbal_precheck_owner;
    std::unique_ptr<gimbal::GimbalVisualTracker> gimbal_tracker_owner;
    gimbal::GimbalPrecheckController* gimbal_precheck = nullptr;
    const auto envEnabled = [](const char* name) {
        const char* value = std::getenv(name);
        return value && std::string(value) == "1";
    };
    // Gimbal is capability-detected by default. The disable switch is an
    // emergency maintenance override, not a normal feature opt-in.
    const bool gimbal_requested = !envEnabled("EDGEFOCUS_DISABLE_GIMBAL");
    if (gimbal_requested) {
        const char* configured_path = std::getenv("EDGEFOCUS_GIMBAL_CONFIG");
        const std::string config_path = configured_path && *configured_path
                                            ? configured_path
                                            : "config/gimbal-board.json";
        const auto board_config = gimbal::loadGimbalBoardConfig(config_path);
        if (!board_config.readyForSession()) {
            LOGE("Main", "Gimbal feature refused: %s", board_config.error.c_str());
        } else {
            gimbal::LinuxGimbalIo::Options io_options;
            io_options.control_mode = board_config.control_mode;
            io_options.uart_device = board_config.uart_device;
            gimbal_service_ptr = std::make_unique<gimbal::GimbalHardwareService>(
                std::make_unique<gimbal::LinuxGimbalIo>(std::move(io_options)),
                board_config.limits);
            gimbal_tracker_owner = std::make_unique<gimbal::GimbalVisualTracker>(
                *gimbal_service_ptr, board_config.tracking);
            gimbal_precheck_owner = std::make_unique<gimbal::GimbalPrecheckController>(
                *gimbal_service_ptr, board_config.precheck,
                gimbal_tracker_owner.get(), [] {
                    return std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                        .count();
                });
            gimbal_precheck = gimbal_precheck_owner.get();
            LOGI("Main", "Gimbal capability enabled with verified board config");
        }
    }
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    auto demo_runtime_ptr = std::make_unique<BoardDemoRuntime>(store);
    BoardDemoRuntime& demo_runtime = *demo_runtime_ptr;
#endif

    session::SessionOrchestrator session_orchestrator(
        session_snapshots,
        [&](const session::SessionContext& context) {
            auto runtime = std::make_unique<session::SessionRuntime>(context);
            runtime->addFeature(
                std::make_unique<DistractionPipelineFeature>(
                    camera_frame_queue, phone_facts, session_snapshots, store,
                    std::string{}, std::string{}, true, &workload_policy),
                true);
            // Formal in-memory Fusion/phone initialization must succeed before
            // the legacy projection performs the SQLite start commit.
            runtime->addFeature(
                std::make_unique<session::LegacySessionProjectionFeature>(store, sqlite_store),
                true);
            if (gimbal_precheck && gimbal_tracker_owner) {
                runtime->addFeature(
                    std::make_unique<gimbal::GimbalSessionFeature>(
                        *gimbal_precheck, *gimbal_tracker_owner, session_snapshots),
                    false);
            }
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
    session_orchestrator.setRuntimeChangedHook([&store] { store.notifySessionChange(); });
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    session_orchestrator.setBeforeStartHook(
        [&demo_runtime](std::string* error) {
            return demo_runtime.acquireForFormalSession(error);
        });
    session_orchestrator.setAfterStopHook(
        [&demo_runtime]() { demo_runtime.releaseFormalSession(); });
#endif

    AppContext ctx;

    // Register modules in dependency order
    auto power_service = std::make_unique<power::PowerSourceService>(
        std::make_unique<power::LinuxI2cPowerSourceIo>(), workload_policy);
    power_service->setSnapshotPublisher(
        [&store](const power::SourceSnapshot& source,
                 const power::ProfileSnapshot& profile,
                 std::int64_t now_ms) {
            auto device = store.snapshot().device;
            device.external_power_state = power::externalPowerStateName(source.state);
            if (source.state == power::ExternalPowerState::Present) {
                device.external_power_present = true;
            } else if (source.state == power::ExternalPowerState::Absent) {
                device.external_power_present = false;
            } else {
                device.external_power_present.reset();
            }
            device.external_power_sample_age_ms = source.sample_available
                ? std::max<std::int64_t>(0, now_ms - source.last_success_ms)
                : -1;
            device.power_profile = profile.effective_profile == power::WorkloadProfile::Home
                ? PowerProfile::HOME
                : PowerProfile::GO;
            device.power_profile_source = power::profileSourceName(profile.source);
            device.manual_override_active = profile.manual_override_active;
            device.power_epoch = profile.power_epoch;
            store.updateDeviceStatus(device);
        });
    ctx.addModule(std::move(power_service));
    if (gimbal_service_ptr) ctx.addModule(std::move(gimbal_service_ptr));
    ctx.addModule(std::make_unique<HealthMonitor>(store));
    ctx.addModule(std::make_unique<CameraPipeline>(camera_frame_queue, store, &preview_frames,
                                                   &session_snapshots));
    auto device_mgr_ptr = std::make_unique<DeviceManager>(store);
    DeviceManager& device_mgr = *device_mgr_ptr;
    ctx.addModule(std::move(device_mgr_ptr));
    auto reminder_policy_ptr = std::make_unique<ReminderPolicy>(store, sqlite_store, device_mgr);
    ReminderPolicy& reminder_policy = *reminder_policy_ptr;
    ctx.addModule(std::move(reminder_policy_ptr));
    ctx.addModule(std::move(sqlite_store_ptr));

    // Create BLE GATT Server
    auto ble_server = std::make_unique<BleGattServer>(store, sqlite_store, reminder_policy,
                                                       reminder_policy.getModeManager(),
                                                       nullptr,
                                                       &session_orchestrator,
                                                       &phone_facts,
                                                       &session_snapshots,
                                                       &workload_policy,
                                                       gimbal_precheck,
                                                       &device_mgr);
    ctx.addModule(std::move(ble_server));

#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    if (demo_http_port > 0) {
        ctx.addModule(std::move(demo_runtime_ptr));
        ctx.addModule(std::make_unique<DemoHttpServer>(store, sqlite_store, device_mgr,
                                                       nullptr, session_orchestrator,
                                                       session_snapshots, phone_facts,
                                                       preview_frames, demo_runtime,
                                                       demo_http_port));
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

    // Close the session admission gate before stopping runtime dependencies.
    session_orchestrator.end(session::StopReason::ProcessShutdown);
    ctx.stopAll();
#ifdef EDGEFOCUS_WITH_DEMO_HTTP
    session_orchestrator.setBeforeStartHook({});
    session_orchestrator.setAfterStopHook({});
#endif
    LOGI("Main", "=== EdgeFocus focus_app stopped ===");
    return 0;
}
