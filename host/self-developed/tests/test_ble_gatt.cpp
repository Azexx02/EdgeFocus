#include "network/BleGattServer.h"
#include "app/AppContext.h"
#include "app/SessionController.h"
#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "policy/ReminderPolicy.h"
#include "device/DeviceManager.h"
#include "device/GimbalMonitor.h"
#include "platform/power/WorkloadProfilePolicy.h"
#include "device/GimbalPrecheckController.h"
#include "session/SessionOrchestrator.h"
#include "session/SessionRuntime.h"
#include "session/SessionSnapshotPublisher.h"
#include "behavior/PhoneFactBuffer.h"
#include <algorithm>
#include <iostream>
#include <cassert>
#include <cmath>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

using namespace edgefocus;
using json = nlohmann::json;

class FakePrecheckGimbalHardware final : public gimbal::IGimbalSessionHardware {
public:
    FakePrecheckGimbalHardware() {
        state.snapshot.calibration_valid = true;
        state.snapshot.origin_valid = true;
        state.snapshot.current_protection_state = "CONFIGURED";
    }
    gimbal::Status status() const override { return state; }
    gimbal::CommandResult enableAndAlign(int) override {
        state.state = gimbal::State::Ready;
        return {true};
    }
    gimbal::CommandResult stopAndDisable(int) override {
        state.state = gimbal::State::Disabled;
        return {true};
    }
    gimbal::CommandResult moveTo(double yaw, double pitch, int) override {
        state.state = gimbal::State::Holding;
        state.yaw_deg = yaw;
        state.pitch_deg = pitch;
        state.snapshot.target_yaw10 = static_cast<std::int32_t>(std::lround(yaw * 10.0));
        state.snapshot.target_pitch10 = static_cast<std::int32_t>(std::lround(pitch * 10.0));
        state.snapshot.motion_generation = ++motion_generation;
        state.snapshot.settled = false;
        gimbal::CommandResult result{true};
        result.queued = true;
        result.firmware_accepted = true;
        result.settled = false;
        result.motion_generation = motion_generation;
        return result;
    }
    gimbal::CommandResult moveToVerified(double yaw, double pitch,
                                         int timeout_ms, double) override {
        return moveTo(yaw, pitch, timeout_ms);
    }
    gimbal::CommandResult moveToManualVerified(double yaw, double pitch,
                                               int timeout_ms, double) override {
        return moveTo(yaw, pitch, timeout_ms);
    }
    gimbal::CommandResult rebaseAlignmentAtCurrent(int) override { return {true}; }
    gimbal::CommandResult trackTo(double yaw, double pitch,
                                  int timeout_ms) override {
        return moveTo(yaw, pitch, timeout_ms);
    }
    gimbal::CommandResult hold(int) override {
        state.state = gimbal::State::Holding;
        return {true};
    }
    gimbal::CommandResult refreshAngle(int) override { return {true}; }
    gimbal::CommandResult freezeAtCurrent(int) override { return {true}; }
    gimbal::CommandResult recover(int) override { return {true}; }
    gimbal::Status state{gimbal::State::Ready, false, false, true, {}, 0.0, 0.0};
    std::uint32_t motion_generation{};
};

std::string decodeBase64(const std::string& input) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> table(256, -1);
    for (int i = 0; i < static_cast<int>(chars.size()); ++i) {
        table[static_cast<unsigned char>(chars[i])] = i;
    }
    std::string out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        if (table[c] == -1) continue;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string assembleChunkPayloads(const std::vector<std::string>& chunks, bool push) {
    std::vector<std::string> parts;
    int total = -1;
    for (const auto& chunk_json : chunks) {
        auto chunk = json::parse(chunk_json);
        json data = push ? chunk["data"] : chunk;
        if (total < 0) {
            total = data["total"].get<int>();
            parts.resize(total);
        }
        parts[data["seq"].get<int>()] = decodeBase64(data["data"].get<std::string>());
    }
    std::string out;
    for (const auto& part : parts) out += part;
    return out;
}

json parseResponse(const std::string& resp) {
    return json::parse(resp);
}

void assertSuccess(const json& resp, int expected_id) {
    assert(resp["status"] == "success");
    assert(resp["id"] == expected_id);
}

int main() {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::cout << "Starting BLE GATT Server tests...\n";
    json response;

    // Test 0: advertisement registration retries and rejects stale callbacks.
    {
        BleAdvertisementRecovery recovery;
        const auto first = recovery.beginAttempt(1'000);
        assert(first.has_value());
        assert(!recovery.beginAttempt(1'000).has_value());
        assert(recovery.complete(*first, false, 1'100));
        assert(recovery.nextRetryMs() == 3'100);
        assert(!recovery.beginAttempt(3'099).has_value());

        const auto second = recovery.beginAttempt(3'100);
        assert(second.has_value());
        recovery.release(3'101);
        assert(!recovery.complete(*second, true, 3'102));
        const auto after_release = recovery.beginAttempt(3'102);
        assert(after_release.has_value());
        assert(recovery.complete(*after_release, true, 3'103));
        assert(recovery.registered());

        recovery.shutdown();
        assert(!recovery.beginAttempt(9'000).has_value());
        std::cout << "  BLE advertisement recovery state machine: OK\n";
    }

    // Test 0a: application chunk payloads can reassemble large responses and pushes.
    {
        std::string large_payload = R"({"status":"success","id":991,"data":{"blob":")" +
            std::string(300, 'x') + R"("}})";
        auto response_chunks = BleGattServer::makeChunkPayloadsForTest(
            large_payload, 991, false, 48);
        assert(response_chunks.size() > 1);
        auto first = json::parse(response_chunks.front());
        assert(first["status"] == "chunk");
        assert(first["id"] == 991);
        assert(first["encoding"] == "base64_utf8");
        assert(assembleChunkPayloads(response_chunks, false) == large_payload);

        std::string push_payload = R"({"cmd":"push_state","data":{"session_phase":"focusing","blob":")" +
            std::string(280, 'p') + R"("}})";
        auto push_chunks = BleGattServer::makeChunkPayloadsForTest(
            push_payload, 0, true, 48);
        assert(push_chunks.size() > 1);
        auto push_first = json::parse(push_chunks.front());
        assert(push_first["cmd"] == "push_chunk");
        assert(push_first["data"]["encoding"] == "base64_utf8");
        assert(assembleChunkPayloads(push_chunks, true) == push_payload);
        std::cout << "  BLE chunk payload reassembly: OK\n";
    }

    // Test 0: startup recovery closes persisted active sessions
    {
        SystemStateStore recovery_store;
        SQLiteStore recovery_sqlite(":memory:");
        AppContext recovery_ctx;
        recovery_sqlite.init(recovery_ctx);
        recovery_sqlite.saveSession("recovery_session", 1000, 60000);
        DeviceManager recovery_dev_mgr(recovery_store);
        ReminderPolicy recovery_policy(recovery_store, recovery_sqlite, recovery_dev_mgr);
        recovery_policy.init(recovery_ctx);
        BleGattServer recovery_server(recovery_store, recovery_sqlite, recovery_policy,
                                      recovery_policy.getModeManager());
        recovery_server.init(recovery_ctx);
        auto recovered = recovery_sqlite.queryLastCompletedSession();
        assert(recovered.session_id == "recovery_session");
        recovery_server.shutdown(recovery_ctx);
        recovery_sqlite.shutdown(recovery_ctx);
        std::cout << "  startup recovery: OK\n";
    }

    // Test 0b: persisted preferences load after SQLite init, not during service construction.
    {
        SystemStateStore pref_store;
        SQLiteStore pref_sqlite(":memory:");
        AppContext pref_ctx;
        pref_sqlite.init(pref_ctx);
        pref_sqlite.savePreference("system_mode", "1");
        pref_sqlite.savePreference("sound_volume", "0.33");
        pref_sqlite.savePreference("screen_output_route", "phone");
        pref_sqlite.savePreference("sound_output_route", "board");
        pref_sqlite.savePreference("gimbal_enabled", "0");
        pref_sqlite.savePreference("normal_channels_mask", "3");
        pref_sqlite.savePreference("silent_channels_mask", "2");
        DeviceManager pref_dev_mgr(pref_store);
        ReminderPolicy pref_policy(pref_store, pref_sqlite, pref_dev_mgr);
        pref_policy.init(pref_ctx);
        BleGattServer pref_server(pref_store, pref_sqlite, pref_policy,
                                  pref_policy.getModeManager());
        pref_server.init(pref_ctx);
        json resp = parseResponse(pref_server.handleCommand(R"({"cmd":"get_preferences","id":2})"));
        assertSuccess(resp, 2);
        assert(resp["data"]["system_mode"] == 1);
        assert(resp["data"]["notification_mode"] == "silent");
        assert(!resp["data"].contains("power_profile"));
        assert(std::fabs(resp["data"]["sound_volume"].get<double>() - 0.33) < 0.001);
        assert(resp["data"]["screen_output_route"] == "phone");
        assert(resp["data"]["sound_output_route"] == "phone");
        assert(resp["data"]["gimbal_enabled"] == false);
        assert(resp["data"]["normal_channels_mask"] == 3);
        assert(resp["data"]["silent_channels_mask"] == 2);
        assert(pref_policy.getModeManager().getMode() == SystemMode::SILENT);
        pref_server.shutdown(pref_ctx);
        pref_sqlite.shutdown(pref_ctx);
        std::cout << "  persisted preferences load after init: OK\n";
    }

    // Test 0c: camera precheck remains available while offline gimbal controls
    // stay hidden from the normal App flow.
    {
        SystemStateStore offline_store;
        SQLiteStore offline_sqlite(":memory:");
        AppContext offline_ctx;
        offline_sqlite.init(offline_ctx);
        DeviceManager offline_dev_mgr(offline_store);
        ReminderPolicy offline_policy(offline_store, offline_sqlite, offline_dev_mgr);
        offline_policy.init(offline_ctx);
        GimbalMonitor offline_gimbal_monitor(offline_store);
        BleGattServer offline_server(offline_store, offline_sqlite, offline_policy,
                                     offline_policy.getModeManager(), &offline_gimbal_monitor);
        offline_server.init(offline_ctx);

        json start_resp = parseResponse(offline_server.handleCommand(
            R"({"cmd":"session_control","id":30,"data":{"action":"start","target_duration_min":25}})"));
        assertSuccess(start_resp, 30);
        assert(start_resp["data"].contains("precheck"));
        assert(start_resp["data"]["precheck"].contains("camera"));
        assert(!start_resp["data"]["precheck"].contains("gimbal"));

        json current_resp = parseResponse(offline_server.handleCommand(
            R"({"cmd":"get_current_state","id":31})"));
        assertSuccess(current_resp, 31);
        assert(current_resp["data"].contains("precheck"));
        assert(current_resp["data"]["precheck"].contains("camera"));
        assert(!current_resp["data"]["precheck"].contains("gimbal"));

        json end_resp = parseResponse(offline_server.handleCommand(
            R"({"cmd":"session_control","id":32,"data":{"action":"end"}})"));
        assertSuccess(end_resp, 32);

        offline_server.shutdown(offline_ctx);
        offline_sqlite.shutdown(offline_ctx);
        std::cout << "  camera precheck shown while offline gimbal hidden: OK\n";
    }

    // Test 0d: the production-disabled gimbal stays hidden even when stale
    // diagnostics say that the controller was online but unsafe.
    {
        SystemStateStore unsafe_store;
        SQLiteStore unsafe_sqlite(":memory:");
        AppContext unsafe_ctx;
        unsafe_sqlite.init(unsafe_ctx);
        DeviceManager unsafe_dev_mgr(unsafe_store);
        ReminderPolicy unsafe_policy(unsafe_store, unsafe_sqlite, unsafe_dev_mgr);
        unsafe_policy.init(unsafe_ctx);
        GimbalMonitor unsafe_gimbal_monitor(unsafe_store);
        BleGattServer unsafe_server(unsafe_store, unsafe_sqlite, unsafe_policy,
                                    unsafe_policy.getModeManager(), &unsafe_gimbal_monitor);
        unsafe_server.init(unsafe_ctx);

        DeviceStatus unsafe_gimbal = unsafe_store.snapshot().device;
        unsafe_gimbal.gimbal_connected = true;
        unsafe_gimbal.gimbal_online = true;
        unsafe_gimbal.gimbal_safe = false;
        unsafe_store.updateDeviceStatus(unsafe_gimbal);

        json start_resp = parseResponse(unsafe_server.handleCommand(
            R"({"cmd":"session_control","id":40,"data":{"action":"start","target_duration_min":25}})"));
        assertSuccess(start_resp, 40);
        assert(start_resp["data"]["precheck"].contains("camera"));
        assert(!start_resp["data"]["precheck"].contains("gimbal"));

        json control_resp = parseResponse(unsafe_server.handleCommand(
            R"({"cmd":"gimbal_control","id":41,"data":{"action":"nudge_left","step_deg":3}})"));
        assert(control_resp["status"] == "error");
        assert(control_resp["error"] == "Gimbal disabled");

        json end_resp = parseResponse(unsafe_server.handleCommand(
            R"({"cmd":"session_control","id":42,"data":{"action":"end"}})"));
        assertSuccess(end_resp, 42);

        unsafe_server.shutdown(unsafe_ctx);
        unsafe_sqlite.shutdown(unsafe_ctx);
        std::cout << "  camera precheck shown while unsafe gimbal hidden: OK\n";
    }

    // Test 0e: stale online/safe status cannot bypass the production feature gate.
    {
        SystemStateStore online_store;
        SQLiteStore online_sqlite(":memory:");
        AppContext online_ctx;
        online_sqlite.init(online_ctx);
        DeviceManager online_dev_mgr(online_store);
        ReminderPolicy online_policy(online_store, online_sqlite, online_dev_mgr);
        online_policy.init(online_ctx);
        GimbalMonitor online_gimbal_monitor(online_store);
        BleGattServer online_server(online_store, online_sqlite, online_policy,
                                    online_policy.getModeManager(), &online_gimbal_monitor);
        online_server.init(online_ctx);

        DeviceStatus gimbal_online = online_store.snapshot().device;
        gimbal_online.gimbal_connected = true;
        gimbal_online.gimbal_online = true;
        gimbal_online.gimbal_safe = true;
        gimbal_online.gimbal_xhome = true;
        gimbal_online.gimbal_yhome = true;
        gimbal_online.gimbal_calibrated = true;
        gimbal_online.gimbal_angle_x = 90.0f;
        gimbal_online.gimbal_angle_y = 0.0f;
        online_store.updateDeviceStatus(gimbal_online);

        json start_resp = parseResponse(online_server.handleCommand(
            R"({"cmd":"session_control","id":43,"data":{"action":"start","target_duration_min":25}})"));
        assertSuccess(start_resp, 43);
        assert(start_resp["data"].contains("precheck"));
        assert(!start_resp["data"]["precheck"].contains("gimbal"));

        json current_resp = parseResponse(online_server.handleCommand(
            R"({"cmd":"get_current_state","id":44})"));
        assertSuccess(current_resp, 44);
        assert(!current_resp["data"]["precheck"].contains("gimbal"));

        json gimbal_ack = parseResponse(online_server.handleCommand(
            R"({"cmd":"gimbal_control","id":46,"data":{"action":"center_x"}})"));
        assert(gimbal_ack["status"] == "error");
        assert(gimbal_ack["error"] == "Gimbal disabled");

        json end_resp = parseResponse(online_server.handleCommand(
            R"({"cmd":"session_control","id":45,"data":{"action":"end"}})"));
        assertSuccess(end_resp, 45);

        online_server.shutdown(online_ctx);
        online_sqlite.shutdown(online_ctx);
        std::cout << "  stale online gimbal cannot bypass feature gate: OK\n";
    }

    // Test 0f: disabled gimbal never enters precheck and gimbal_control
    // returns disabled even if stale device flags suggest it was once online.
    {
        SystemStateStore disabled_store;
        SQLiteStore disabled_sqlite(":memory:");
        AppContext disabled_ctx;
        disabled_sqlite.init(disabled_ctx);
        disabled_sqlite.savePreference("gimbal_enabled", "0");
        DeviceManager disabled_dev_mgr(disabled_store);
        ReminderPolicy disabled_policy(disabled_store, disabled_sqlite, disabled_dev_mgr);
        disabled_policy.init(disabled_ctx);
        GimbalMonitor disabled_gimbal_monitor(disabled_store);
        BleGattServer disabled_server(disabled_store, disabled_sqlite, disabled_policy,
                                      disabled_policy.getModeManager(), &disabled_gimbal_monitor);
        disabled_server.init(disabled_ctx);

        DeviceStatus stale_online = disabled_store.snapshot().device;
        stale_online.gimbal_connected = true;
        stale_online.gimbal_online = true;
        stale_online.gimbal_safe = true;
        disabled_store.updateDeviceStatus(stale_online);

        json start_resp = parseResponse(disabled_server.handleCommand(
            R"({"cmd":"session_control","id":36,"data":{"action":"start","target_duration_min":25}})"));
        assertSuccess(start_resp, 36);
        assert(start_resp["data"].contains("precheck"));
        assert(!start_resp["data"]["precheck"].contains("gimbal"));

        json current_resp = parseResponse(disabled_server.handleCommand(
            R"({"cmd":"get_current_state","id":37})"));
        assertSuccess(current_resp, 37);
        assert(!current_resp["data"]["precheck"].contains("gimbal"));

        json control_resp = parseResponse(disabled_server.handleCommand(
            R"({"cmd":"gimbal_control","id":38,"data":{"action":"nudge_left","step_deg":3}})"));
        assert(control_resp["status"] == "error");
        assert(control_resp["id"] == 38);
        assert(control_resp["error"] == "Gimbal disabled");

        json end_resp = parseResponse(disabled_server.handleCommand(
            R"({"cmd":"session_control","id":39,"data":{"action":"end"}})"));
        assertSuccess(end_resp, 39);

        disabled_server.shutdown(disabled_ctx);
        disabled_sqlite.shutdown(disabled_ctx);
        std::cout << "  precheck.gimbal hidden when disabled: OK\n";
    }

    // Test 0g: the new PRECHECK command surface is session-scoped and only
    // advertised when a typed controller is actually wired.
    {
        SystemStateStore typed_store;
        SQLiteStore typed_sqlite(":memory:");
        AppContext typed_ctx;
        typed_sqlite.init(typed_ctx);
        DeviceManager typed_devices(typed_store);
        ReminderPolicy typed_policy(typed_store, typed_sqlite, typed_devices);
        typed_policy.init(typed_ctx);
        session::SessionSnapshotPublisher typed_snapshots("typed-gimbal-test");
        std::int64_t monotonic = 1000;
        session::SessionOrchestrator typed_orchestrator(
            typed_snapshots,
            [](const session::SessionContext& context) {
                return std::make_unique<session::SessionRuntime>(context);
            },
            [&] { return monotonic; },
            [] { return std::int64_t{2000}; });
        const auto started = typed_orchestrator.start(
            {1500000, session::SourceMode::LiveCamera});
        assert(started.success && started.context);

        FakePrecheckGimbalHardware hardware;
        gimbal::PrecheckConfig config;
        config.search_waypoints = {{-5.0, 0.0}, {5.0, 0.0}};
        config.center = gimbal::Waypoint{0.0, 0.0};
        config.normal_nudge_step_deg = 2.0;
        config.fine_nudge_step_deg = 1.0;
        gimbal::GimbalPrecheckController controller(hardware, config);
        assert(controller.start(started.context->session_id, monotonic));
        PhoneFactBuffer typed_phone_facts;

        BleGattServer typed_server(
            typed_store, typed_sqlite, typed_policy, typed_policy.getModeManager(),
            nullptr, &typed_orchestrator, &typed_phone_facts, &typed_snapshots, nullptr, &controller);
        typed_server.init(typed_ctx);
        json precheck_state = parseResponse(typed_server.handleCommand(
            R"({"cmd":"get_current_state","id":46})"));
        assertSuccess(precheck_state, 46);
        assert(precheck_state["data"]["runtime_snapshot"]["session"]["runtime_active"] == true);
        assert(precheck_state["data"]["runtime_snapshot"]["session"]["active"] == false);
        json premature_phone = parseResponse(typed_server.handleCommand(
            R"({"cmd":"phone_sync","id":461,"data":{"is_in_use":true,"usage_state":"playing"}})"));
        assertSuccess(premature_phone, 461);
        assert(premature_phone["data"]["accepted"] == false);
        assert(premature_phone["data"]["error_code"] == "formal_session_not_started");
        json hello = parseResponse(typed_server.handleCommand(
            R"({"cmd":"client_hello","id":47,"data":{"capabilities":[]}})"));
        assertSuccess(hello, 47);
        assert(std::find(hello["data"]["server_capabilities"].begin(),
                         hello["data"]["server_capabilities"].end(),
                         "gimbal_session_control_v3") != hello["data"]["server_capabilities"].end());
        assert(std::find(hello["data"]["server_capabilities"].begin(),
                         hello["data"]["server_capabilities"].end(),
                         "gimbal_async_result_v1") != hello["data"]["server_capabilities"].end());

        const auto wait_until = [](const auto& predicate) {
            for (int i = 0; i < 100; ++i) {
                if (predicate()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return false;
        };
        std::mutex pushed_mutex;
        std::vector<json> pushed_results;
        std::vector<json> pushed_gimbal_states;
        typed_store.setPushCallback([&](const std::string& payload) {
            const auto parsed = json::parse(payload, nullptr, false);
            if (parsed.is_discarded()) return;
            std::lock_guard<std::mutex> lock(pushed_mutex);
            if (parsed.value("cmd", "") == "push_gimbal_command_result") {
                pushed_results.push_back(parsed["data"]);
            } else if (parsed.value("cmd", "") ==
                       "push_gimbal_precheck_state") {
                pushed_gimbal_states.push_back(parsed["data"]);
            }
        });
        const auto wait_for_result = [&](const std::string& command,
                                         std::uint64_t sequence,
                                         std::size_t occurrence = 1) {
            for (int i = 0; i < 100; ++i) {
                {
                    std::lock_guard<std::mutex> lock(pushed_mutex);
                    std::size_t matches = 0;
                    for (const auto& item : pushed_results) {
                        if (item.value("command", "") == command &&
                            item.value("request_sequence", std::uint64_t{}) == sequence) {
                            if (++matches == occurrence) return item;
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return json::object();
        };

        session::GimbalSnapshot degraded_snapshot;
        degraded_snapshot.state = "degraded";
        degraded_snapshot.health = {false, true, "recovering", "STALL", monotonic};
        degraded_snapshot.fault_class = "motion";
        degraded_snapshot.fault_code = "STALL";
        degraded_snapshot.detail = "runtime_fault";
        typed_snapshots.updateGimbal(degraded_snapshot, monotonic);
        assert(wait_until([&] {
            std::lock_guard<std::mutex> lock(pushed_mutex);
            return pushed_gimbal_states.size() == 1;
        }));
        {
            std::lock_guard<std::mutex> lock(pushed_mutex);
            const auto& pushed = pushed_gimbal_states.back();
            assert(pushed["degraded"] == true);
            assert(!pushed["controls"].contains("stop_enabled"));
            assert(pushed["controls"].size() == 2);
        }
        typed_snapshots.updateGimbal(degraded_snapshot, monotonic + 1);
        {
            std::lock_guard<std::mutex> lock(pushed_mutex);
            assert(pushed_gimbal_states.size() == 1);
        }

        const auto skip_payload = std::string(
            "{\"cmd\":\"precheck_skip_auto_adjust\",\"id\":48,\"data\":{"
            "\"session_id\":\"") + started.context->session_id +
            "\",\"sequence\":1}}";
        json skipped = parseResponse(typed_server.handleCommand(skip_payload));
        assertSuccess(skipped, 48);
        assert(skipped["data"]["queued"] == true);
        assert(wait_until([&] {
            return controller.snapshot().state == gimbal::PrecheckState::ManualAvailable;
        }));
        const auto skip_result = wait_for_result("precheck_skip_auto_adjust", 1);
        assert(skip_result.value("accepted", false));

        const auto nudge_payload = std::string(
            "{\"cmd\":\"gimbal_nudge\",\"id\":49,\"data\":{"
            "\"session_id\":\"") + started.context->session_id +
            "\",\"owner_id\":\"app-test\",\"sequence\":1,\"direction\":\"RIGHT\"}}";
        json nudged = parseResponse(typed_server.handleCommand(nudge_payload));
        assertSuccess(nudged, 49);
        assert(nudged["data"]["queued"] == true);
        assert(wait_until([&] { return hardware.state.yaw_deg == 2.0; }));
        const auto nudge_result = wait_for_result("gimbal_nudge", 1);
        assert(nudge_result.value("accepted", false));
        assert(nudge_result.value("firmware_accepted", false));
        assert(nudge_result.value("acked_through_sequence", std::uint64_t{}) == 1);
        assert(nudge_result["settled"] == false);

        assert(typed_orchestrator.confirmReady().success);
        controller.holdForSession(monotonic);
        const auto takeover_payload = std::string(
            "{\"cmd\":\"gimbal_manual_takeover\",\"id\":481,\"data\":{"
            "\"session_id\":\"") + started.context->session_id +
            "\",\"owner_id\":\"app-test\",\"sequence\":2}}";
        json takeover = parseResponse(typed_server.handleCommand(takeover_payload));
        assertSuccess(takeover, 481);
        assert(takeover["data"]["queued"] == true);
        assert(wait_until([&] { return controller.snapshot().manual_lease; }));
        const auto takeover_result = wait_for_result("gimbal_manual_takeover", 2);
        assert(takeover_result.value("accepted", false));
        const auto lease_token = takeover_result.value("lease_token", "");
        assert(!lease_token.empty());

        const auto session_nudge_payload = std::string(
            "{\"cmd\":\"gimbal_nudge\",\"id\":492,\"data\":{"
            "\"session_id\":\"") + started.context->session_id +
            "\",\"owner_id\":\"app-test\",\"lease_token\":\"" + lease_token +
            "\",\"sequence\":3,\"direction\":\"LEFT\"}}";
        json session_nudge =
            parseResponse(typed_server.handleCommand(session_nudge_payload));
        assertSuccess(session_nudge, 492);
        assert(session_nudge["data"]["queued"] == true);
        const auto session_nudge_result = wait_for_result("gimbal_nudge", 3);
        assert(session_nudge_result.value("accepted", false));

        typed_orchestrator.end(session::StopReason::Cancelled);
        monotonic += 1000;
        const auto precheck_only = typed_orchestrator.start(
            {1500000, session::SourceMode::LiveCamera});
        assert(precheck_only.success);
        controller.stop();
        assert(controller.start(precheck_only.context->session_id, monotonic));

        const auto force_stop_payload = std::string(
            "{\"cmd\":\"gimbal_stop\",\"id\":493,\"data\":{"
            "\"session_id\":\"") + precheck_only.context->session_id +
            "\",\"owner_id\":\"app-test\",\"sequence\":1}}";
        json force_stopped =
            parseResponse(typed_server.handleCommand(force_stop_payload));
        assertSuccess(force_stopped, 493);
        assert(force_stopped["data"]["queued"] == true);
        assert(wait_until([&] {
            return controller.snapshot().state == gimbal::PrecheckState::Released;
        }));
        const auto force_stop_result = wait_for_result("gimbal_stop", 1);
        assert(force_stop_result.value("accepted", false));
        assert(hardware.state.state == gimbal::State::Disabled);

        json stale_cancel = parseResponse(typed_server.handleCommand(
            R"({"cmd":"session_control","id":490,"data":{"action":"end","session_id":"stale-session"}})"));
        assertSuccess(stale_cancel, 490);
        assert(stale_cancel["data"]["success"] == false);
        assert(stale_cancel["data"]["message"] == "session_id_mismatch");
        assert(typed_orchestrator.phase() == session::Phase::Precheck);

        const auto cancel_payload = std::string(
            "{\"cmd\":\"session_control\",\"id\":491,\"data\":{"
            "\"action\":\"end\",\"session_id\":\"") +
            precheck_only.context->session_id + "\"}}";
        json cancelled_precheck = parseResponse(typed_server.handleCommand(
            cancel_payload));
        assertSuccess(cancelled_precheck, 491);
        assert(cancelled_precheck["data"]["success"] == true);
        assert(cancelled_precheck["data"]["cancelled_precheck"] == true);
        assert(cancelled_precheck["data"]["formal_session_started"] == false);
        assert(!cancelled_precheck["data"].contains("session_id"));
        controller.stop();
        typed_store.setPushCallback({});
        typed_server.shutdown(typed_ctx);
        typed_sqlite.shutdown(typed_ctx);
        std::cout << "  typed PRECHECK gimbal command surface: OK\n";
    }

    SystemStateStore store;
    SQLiteStore sqlite_store(":memory:");
    AppContext ctx;
    sqlite_store.init(ctx);

    DeviceManager dev_mgr(store);
    dev_mgr.init(ctx);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    policy.init(ctx);
    power::WorkloadProfilePolicy workload_policy;
    BleGattServer server(store, sqlite_store, policy, policy.getModeManager(),
                         nullptr, nullptr, nullptr, nullptr, &workload_policy, nullptr, &dev_mgr);
    server.init(ctx);

    // Test 1: get_device_status
    {
        json resp = parseResponse(server.handleCommand(R"({"cmd":"get_device_status","id":1})"));
        assertSuccess(resp, 1);
        assert(!resp["data"].contains("battery_percent"));
        assert(!resp["data"].contains("battery_voltage_mv"));
        assert(!resp["data"].contains("battery_charging"));
        assert(!resp["data"].contains("power_source"));
        assert(resp["data"]["external_power_state"] == "UNKNOWN");
        assert(resp["data"]["external_power_present"].is_null());
        assert(resp["data"]["external_power_sample_age_ms"].is_null());
        assert(resp["data"].contains("power_profile"));
        assert(resp["data"]["power_profile"] == "go");
        assert(resp["data"].contains("system_mode"));
        assert(resp["data"].contains("gimbal_connected"));
        assert(resp["data"].contains("gimbal_online"));
        assert(resp["data"].contains("board_screen_available"));
        assert(resp["data"].contains("board_sound_available"));
        assert(resp["data"].contains("output_capability"));
        std::cout << "  get_device_status: OK\n";
    }

    // Test 2: get_current_state
    {
        json resp = parseResponse(server.handleCommand(R"({"cmd":"get_current_state","id":2})"));
        assertSuccess(resp, 2);
        assert(resp["data"].contains("focus_state"));
        assert(resp["data"].contains("session_phase"));
        assert(resp["data"].contains("reminder"));
        assert(resp["data"].contains("output_capability"));
        assert(resp["data"].contains("precheck_guidance"));
        assert(resp["data"].contains("camera_preview"));
        assert(resp["data"]["session_phase"] == "idle");
        assert(resp["data"].contains("elapsed_ms"));
        assert(resp["data"].contains("active"));
        assert(resp["data"].contains("session_id"));
        assert(!resp["data"].contains("battery_percent"));
        assert(!resp["data"].contains("battery_charging"));
        assert(!resp["data"].contains("power_source"));
        assert(resp["data"]["external_power_state"] == "UNKNOWN");
        assert(resp["data"]["external_power_present"].is_null());
        assert(resp["data"]["power_profile"] == "go");
        assert(resp["data"]["power_profile_source"] == "auto_failsafe");
        assert(resp["data"]["manual_override_active"] == false);
        std::cout << "  get_current_state: OK\n";
    }

    // Test 2a: Home/Go is an explicit, non-persisted runtime override.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"set_power_profile","id":201,"data":{"power_profile":"home"}})"));
        assertSuccess(resp, 201);
        assert(resp["data"]["power_profile"] == "home");
        assert(resp["data"]["power_profile_source"] == "manual");
        assert(resp["data"]["manual_override_active"] == true);

        json status = parseResponse(server.handleCommand(
            R"({"cmd":"get_device_status","id":203})"));
        assertSuccess(status, 203);
        assert(status["data"]["power_profile"] == "home");
        assert(status["data"]["manual_override_active"] == true);

        json reset = parseResponse(server.handleCommand(
            R"({"cmd":"set_power_profile","id":204,"data":{"power_profile":"go"}})"));
        assertSuccess(reset, 204);
        assert(reset["data"]["power_profile"] == "go");
        std::cout << "  explicit runtime power profile override: OK\n";
    }

    // Test 2b: public state payload keeps focus_state compatible and exposes
    // phone subtype details in dedicated fields.
    {
        BehaviorContext phone_ctx;
        phone_ctx.risk_score = 50;
        phone_ctx.phone_signal = true;
        phone_ctx.phone_usage_state = static_cast<int>(PhoneUsageState::PLAYING);
        phone_ctx.phone_distraction_level = 2;
        phone_ctx.reason = "phone_sync";
        store.updateBehaviorContext(phone_ctx);
        store.updateFocusState(FocusState::PHONE_IN_PLAYING, 1234, "Phone sync signal");

        json resp = parseResponse(server.handleCommand(R"({"cmd":"get_current_state","id":202})"));
        assertSuccess(resp, 202);
        assert(resp["data"]["focus_state"] == static_cast<int>(FocusState::PHONE_IN_USE));
        assert(resp["data"]["phone_signal"] == true);
        assert(resp["data"]["phone_usage_state"] == "playing");
        assert(resp["data"]["phone_distraction_level"] == 2);
        assert(resp["data"]["risk_score"] == 50);
        std::cout << "  get_current_state phone subtype fields: OK\n";
    }

    // Test 3: get_today_summary
    {
        json resp = parseResponse(server.handleCommand(R"({"cmd":"get_today_summary","id":3})"));
        assertSuccess(resp, 3);
        assert(resp["data"].contains("total_seconds"));
        assert(resp["data"].contains("focus_seconds"));
        assert(resp["data"].contains("phone_distraction_count"));
        std::cout << "  get_today_summary: OK\n";
    }

    // Test 4: get_last_report (no report yet)
    {
        json resp = parseResponse(server.handleCommand(R"({"cmd":"get_last_report","id":4})"));
        assertSuccess(resp, 4);
        assert(resp["data"].is_null());
        std::cout << "  get_last_report (empty): OK\n";
    }

    // Test 4b: start without duration still uses the board default target.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":41,"data":{"action":"start"}})"));
        assertSuccess(resp, 41);
        assert(resp["data"]["success"] == true);
        assert(resp["data"]["target_duration_ms"] == 25 * 60000);
        auto snap = store.snapshot();
        assert(snap.session.target_duration_ms == 25 * 60000);

        json end_resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":42,"data":{"action":"end"}})"));
        assertSuccess(end_resp, 42);
        std::cout << "  session_control start default duration: OK\n";
    }

    // Test 4c: requested focus durations are clamped to the supported range.
    {
        json too_short = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":43,"data":{"action":"start","target_duration_min":1}})"));
        assertSuccess(too_short, 43);
        assert(too_short["data"]["success"] == true);
        assert(too_short["data"]["target_duration_ms"] == 5 * 60000);
        server.handleCommand(R"({"cmd":"session_control","id":44,"data":{"action":"end"}})");

        json too_long = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":45,"data":{"action":"start","target_duration_min":9999}})"));
        assertSuccess(too_long, 45);
        assert(too_long["data"]["success"] == true);
        assert(too_long["data"]["target_duration_ms"] == 120 * 60000);
        server.handleCommand(R"({"cmd":"session_control","id":46,"data":{"action":"end"}})");
        std::cout << "  session_control start duration clamp: OK\n";
    }

    // Test 5: session_control start
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":5,"data":{"action":"start","target_duration_min":25}})"));
        assertSuccess(resp, 5);
        assert(resp["data"]["success"] == true);
        assert(resp["data"].contains("session_id"));
        assert(resp["data"].contains("remaining_ms"));
        assert(resp["data"].contains("target_duration_ms"));
        assert(resp["data"]["target_duration_ms"] == 1500000);
        assert(resp["data"].contains("focus_state"));
        assert(resp["data"]["focus_state"] == static_cast<int>(FocusState::CALIBRATING));
        assert(resp["data"].contains("calibration_completed"));
        assert(resp["data"]["calibration_completed"] == false);
        assert(resp["data"].contains("precheck"));
        assert(resp["data"]["precheck"].contains("camera"));
        assert(!resp["data"]["precheck"].contains("gimbal"));
        auto snap = store.snapshot();
        assert(snap.session.active == true);
        assert(snap.session.target_duration_ms == 25 * 60000);
        assert(snap.session.start_ms > 0);
        assert(snap.session.start_ms <= snap.snapshot_ms);

        json precheck_state = parseResponse(server.handleCommand(
            R"({"cmd":"get_current_state","id":49})"));
        assertSuccess(precheck_state, 49);
        assert(precheck_state["data"]["session_phase"] == "precheck");
        assert(precheck_state["data"].contains("precheck"));
        assert(precheck_state["data"]["precheck"].contains("camera"));
        assert(!precheck_state["data"]["precheck"].contains("gimbal"));

        json skip_resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":51,"data":{"action":"skip_precheck"}})"));
        assertSuccess(skip_resp, 51);
        assert(skip_resp["data"]["success"] == true);
        assert(skip_resp["data"]["session_phase"] == "focusing");
        assert(skip_resp["data"]["calibration_completed"] == true);
        assert(skip_resp["data"]["elapsed_ms"] == 0);

        json focused_state = parseResponse(server.handleCommand(
            R"({"cmd":"get_current_state","id":50})"));
        assertSuccess(focused_state, 50);
        assert(focused_state["data"]["session_phase"] == "focusing");
        std::cout << "  session_control start: OK\n";
    }

    // Test 6: session_control start (idempotent)
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":6,"data":{"action":"start","target_duration_min":25}})"));
        assertSuccess(resp, 6);
        assert(resp["data"]["success"] == true);
        assert(resp["data"]["already_active"] == true);
        assert(resp["data"].contains("remaining_ms"));
        std::cout << "  session_control start (idempotent): OK\n";
    }

    // Test 7: get_current_state (active session)
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"get_current_state","id":7})"));
        assert(resp["data"]["target_duration_ms"] == 1500000);
        assert(resp["data"]["active"] == true);
        assert(resp["data"]["session_phase"] == "focusing");
        std::cout << "  get_current_state (active): OK\n";
    }

    // Test 8: session_control pause
    {
        auto pre_pause = store.snapshot().session.session_id;
        std::string last_push;
        store.setPushCallback([&last_push](const std::string& event_json) {
            last_push = event_json;
        });
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":8,"data":{"action":"pause"}})"));
        assertSuccess(resp, 8);
        assert(resp["data"]["success"] == true);
        assert(resp["data"]["paused"] == true);
        assert(resp["data"]["session_phase"] == "paused");
        auto snap = store.snapshot();
        assert(snap.session.session_id == pre_pause);
        assert(snap.session.paused == true);
        assert(snap.session.active == true);
        auto push = json::parse(last_push);
        assert(push["cmd"] == "push_state");
        assert(push["data"]["session_phase"] == "paused");
        store.setPushCallback({});
        assert(server.init(ctx));
        std::cout << "  session_control pause: OK\n";
    }

    // Test 9: session_control resume
    {
        auto paused_id = store.snapshot().session.session_id;
        std::string last_push;
        store.setPushCallback([&last_push](const std::string& event_json) {
            last_push = event_json;
        });
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":9,"data":{"action":"resume"}})"));
        assertSuccess(resp, 9);
        assert(resp["data"]["success"] == true);
        assert(resp["data"]["paused"] == false);
        assert(resp["data"]["session_phase"] == "focusing");
        assert(resp["data"].contains("remaining_ms"));
        auto snap = store.snapshot();
        assert(snap.session.session_id == paused_id);
        assert(snap.session.paused == false);
        assert(snap.session.active == true);
        auto push = json::parse(last_push);
        assert(push["cmd"] == "push_state");
        assert(push["data"]["session_phase"] == "focusing");
        store.setPushCallback({});
        assert(server.init(ctx));
        std::cout << "  session_control resume: OK\n";
    }

    // Test 10: session_control end
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":10,"data":{"action":"end"}})"));
        assertSuccess(resp, 10);
        assert(resp["data"]["success"] == true);
        assert(resp["data"].contains("total_duration"));
        assert(resp["data"].contains("focus_duration"));
        assert(resp["data"].contains("distraction_count"));
        assert(resp["data"].contains("reminder_count"));
        auto snap = store.snapshot();
        assert(snap.session.active == false);
        std::cout << "  session_control end: OK\n";
    }

    // Test 10b: BLE cannot skip a board-owned precheck.
    {
        SessionController board_controller(store, sqlite_store);
        auto started = board_controller.startSession(25, "board");
        assert(started.success == true);
        auto snap = store.snapshot();
        assert(snap.session.active == true);
        assert(snap.session.precheck_owner == "board");

        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":101,"data":{"action":"skip_precheck"}})"));
        assertSuccess(resp, 101);
        assert(resp["data"]["success"] == false);
        assert(resp["data"]["message"].get<std::string>().find("not allowed") != std::string::npos);
        board_controller.endSession("cleanup_board_owned_precheck");
        std::cout << "  session_control skip board-owned precheck denied: OK\n";
    }

    // Test 11a: defaults are SCREEN+BAND+SOUND / SCREEN+BAND.
    {
        json resp = parseResponse(server.handleCommand(R"({"cmd":"get_preferences","id":1110})"));
        assertSuccess(resp, 1110);
        assert(resp["data"]["normal_channels_mask"] == 7);
        assert(resp["data"]["silent_channels_mask"] == 3);
        assert(resp["data"]["sound_volume"] == 0.7f);
        assert(resp["data"]["screen_output_route"] == "phone");
        assert(resp["data"]["sound_output_route"] == "phone");
        assert(resp["data"]["gimbal_enabled"] == false);
        assert(resp["data"]["normal_channels"] == json::array({"SCREEN", "BAND", "SOUND"}));
        assert(resp["data"]["silent_channels"] == json::array({"SCREEN", "BAND"}));
        assert(resp["data"]["notification_mode"] == "normal");
        assert(!resp["data"].contains("power_profile"));
        std::cout << "  get_preferences defaults: OK\n";
    }

    // Test 11: update_preferences
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":11,"data":{"notification_mode":"silent","default_focus_duration_min":35,"sound_volume":0.25,"normal_channels_mask":3,"silent_channels_mask":2}})"));
        assertSuccess(resp, 11);
        assert(resp["data"]["notification_mode"] == "silent");
        assert(!resp["data"].contains("power_profile"));
        assert(resp["data"]["default_focus_duration_min"] == 35);
        assert(resp["data"]["sound_volume"] == 0.25f);
        assert(policy.getModeManager().getMode() == SystemMode::SILENT);
        assert(resp["data"]["normal_channels_mask"] == 3);
        assert(resp["data"]["silent_channels_mask"] == 2);
        assert(resp["data"]["normal_channels"] == json::array({"SCREEN", "BAND"}));
        assert(resp["data"]["silent_channels"] == json::array({"BAND"}));
        std::cout << "  update_preferences: OK\n";
    }

    // Test 11b: update_preferences rejects invalid field types without throwing.
    {
        bool threw = false;
        json resp;
        try {
            resp = parseResponse(server.handleCommand(
                R"({"cmd":"update_preferences","id":111,"data":{"system_mode":"silent"}})"));
        } catch (...) {
            threw = true;
        }
        assert(!threw);
        assert(resp["status"] == "error");
        assert(resp["id"] == 111);
        std::cout << "  update_preferences invalid type: OK\n";
    }

    // Home/Go is a transient power policy override, not a persisted preference.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":1110,"data":{"power_profile":"home"}})"));
        assert(resp["status"] == "error");
        assert(resp["id"] == 1110);
        assert(resp["error"].get<std::string>().find("set_power_profile") != std::string::npos);
        std::cout << "  update_preferences rejects power_profile: OK\n";
    }

    // Test 11b.1: update_preferences rejects unknown system_mode enum values.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":1111,"data":{"system_mode":99}})"));
        assert(resp["status"] == "error");
        assert(resp["id"] == 1111);
        assert(policy.getModeManager().getMode() == SystemMode::SILENT);
        std::cout << "  update_preferences invalid system_mode enum: OK\n";
    }

    // Test 11c: update_preferences accepts channel arrays and updates masks.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":112,"data":{"normal_channels":["light","band","sound"],"silent_channels":["screen","sound","band"]}})"));
        assertSuccess(resp, 112);
        assert(resp["data"]["normal_channels_mask"] == 7);
        assert(resp["data"]["silent_channels_mask"] == 3);
        assert(resp["data"]["normal_channels"] == json::array({"SCREEN", "BAND", "SOUND"}));
        assert(resp["data"]["silent_channels"] == json::array({"SCREEN", "BAND"}));
        std::cout << "  update_preferences normal/silent arrays with legacy LIGHT and silent SOUND filter: OK\n";
    }

    // Test 11d: update_preferences silently filters SOUND from silent numeric mask.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":113,"data":{"silent_channels_mask":7}})"));
        assertSuccess(resp, 113);
        assert(resp["data"]["silent_channels_mask"] == 3);
        assert(resp["data"]["silent_channels"] == json::array({"SCREEN", "BAND"}));
    }

    // Test 11e: update_preferences clamps sound_volume into 0.0..1.0.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":114,"data":{"sound_volume":1.5}})"));
        assertSuccess(resp, 114);
        assert(resp["data"]["sound_volume"] == 1.0f);
    }

    // Test 11f: update_preferences stores independent board/phone output routes.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":115,"data":{"screen_output_route":"phone","sound_output_route":"board","gimbal_enabled":false}})"));
        assertSuccess(resp, 115);
        assert(resp["data"]["screen_output_route"] == "phone");
        assert(resp["data"]["sound_output_route"] == "phone");
        assert(resp["data"]["gimbal_enabled"] == false);
    }

    // Test 11g: update_preferences clamps out-of-range client values.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":116,"data":{"away_timeout_min":999,"default_focus_duration_min":999,"sound_volume":2.0}})"));
        assertSuccess(resp, 116);
        assert(resp["data"]["away_timeout_min"] == 180);
        assert(resp["data"]["default_focus_duration_min"] == 120);
        assert(resp["data"]["sound_volume"] == 1.0);

        json reset = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":117,"data":{"away_timeout_min":7,"default_focus_duration_min":35,"sound_volume":0.7}})"));
        assertSuccess(reset, 117);
        std::cout << "  update_preferences clamps values: OK\n";
    }

    // Test 12: get_preferences
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"get_preferences","id":12})"));
        assertSuccess(resp, 12);
        assert(resp["data"].contains("system_mode"));
        assert(resp["data"].contains("notification_mode"));
        assert(!resp["data"].contains("power_profile"));
        assert(resp["data"]["notification_mode"] == "silent");
        assert(resp["data"].contains("sound_volume"));
        assert(resp["data"].contains("away_timeout_min"));
        assert(resp["data"].contains("default_focus_duration_min"));
        assert(resp["data"]["default_focus_duration_min"] == 35);
        assert(resp["data"].contains("normal_channels_mask"));
        assert(resp["data"].contains("silent_channels_mask"));
        assert(resp["data"].contains("normal_channels"));
        assert(resp["data"].contains("silent_channels"));
        assert(resp["data"]["screen_output_route"] == "phone");
        assert(resp["data"]["sound_output_route"] == "phone");
        assert(resp["data"]["gimbal_enabled"] == false);
        std::cout << "  get_preferences: OK\n";
    }

    // Test 12b: start without duration uses configured default focus duration.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":121,"data":{"action":"start"}})"));
        assertSuccess(resp, 121);
        assert(resp["data"]["success"] == true);
        assert(resp["data"]["target_duration_ms"] == 35 * 60000);
        json end_resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":122,"data":{"action":"end"}})"));
        assertSuccess(end_resp, 122);
        std::cout << "  session_control start configured default duration: OK\n";
    }

    // Test 12c: idle away-time preference should affect the next session
    // without relying on a stale inactive SessionInfo overwrite.
    {
        json prefs_resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":123,"data":{"away_timeout_min":7}})"));
        assertSuccess(prefs_resp, 123);
        json start_resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":124,"data":{"action":"start","target_duration_min":10}})"));
        assertSuccess(start_resp, 124);
        assert(store.snapshot().session.away_timeout_ms == 7 * 60000);
        json end_resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":125,"data":{"action":"end"}})"));
        assertSuccess(end_resp, 125);
        std::cout << "  idle away timeout applies to next session: OK\n";
    }

    // Test 13: reminder_ack
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"reminder_ack","id":13,"data":{}})"));
        assertSuccess(resp, 13);
        assert(resp["data"].contains("cooling_ms"));
        std::cout << "  reminder_ack: OK\n";
    }

    // Test 14: phone_sync can fall back from distraction_level when no usage_state is present.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":14,"data":{"is_in_use":true,"distraction_level":2,"app_name":"com.test.app"}})"));
        assertSuccess(resp, 14);
        assert(resp["data"].contains("focus_state"));
        assert(resp["data"].contains("action_taken"));
        assert(resp["data"].contains("distraction_count"));
        assert(resp["data"].contains("reminder_count"));
        assert(resp["data"]["action_taken"] == true);
        PhoneUsageSignal sig;
        assert(store.consumePhoneSignal(sig));
        assert(sig.active == true);
        assert(sig.distraction_level == 2);
        assert(sig.usage_state == PhoneUsageState::PLAYING);
        std::cout << "  phone_sync level fallback: OK\n";
    }

    // Test 14b: explicit usage_state wins over distraction_level; distraction_level is retained for diagnostics.
    {
        json resp_state = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":114,"data":{"is_in_use":true,"distraction_level":3,"app_name":"unknown"}})"));
        assertSuccess(resp_state, 114);
        PhoneUsageSignal sig;
        assert(store.consumePhoneSignal(sig));
        assert(sig.active == true);
        assert(sig.distraction_level == 3);
        assert(sig.usage_state == PhoneUsageState::IN_USE);

        json resp_usage_wins_learning = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":116,"data":{"is_in_use":true,"usage_state":"learning","distraction_level":2,"app_name":"study_app"}})"));
        assertSuccess(resp_usage_wins_learning, 116);
        assert(store.consumePhoneSignal(sig));
        assert(sig.active == true);
        assert(sig.distraction_level == 2);
        assert(sig.usage_state == PhoneUsageState::LEARNING);

        json resp_usage_wins_playing = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":115,"data":{"is_in_use":true,"usage_state":"playing","distraction_level":0,"app_name":"video"}})"));
        assertSuccess(resp_usage_wins_playing, 115);
        assert(store.consumePhoneSignal(sig));
        assert(sig.active == true);
        assert(sig.distraction_level == 0);
        assert(sig.usage_state == PhoneUsageState::PLAYING);
        std::cout << "  phone_sync explicit usage_state priority: OK\n";
    }

    // Test 15: phone_sync (not in use)
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":15,"data":{"is_in_use":false,"distraction_level":0}})"));
        assertSuccess(resp, 15);
        assert(resp["data"]["action_taken"] == false);
        PhoneUsageSignal sig;
        assert(store.consumePhoneSignal(sig));
        assert(sig.active == false);
        assert(sig.distraction_level == 0);
        std::cout << "  phone_sync (not in use): OK\n";
    }

    // Test 15b: phone_sync allows learning/level 0 as a non-distraction signal.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":151,"data":{"is_in_use":true,"app_category":"learning","usage_state":"learning","distraction_level":0,"app_name":"com.edgefocus.app"}})"));
        assertSuccess(resp, 151);
        PhoneUsageSignal sig;
        assert(store.consumePhoneSignal(sig));
        assert(sig.active == true);
        assert(sig.distraction_level == 0);
        assert(sig.usage_state == PhoneUsageState::LEARNING);
        std::cout << "  phone_sync learning level 0: OK\n";
    }

    // Test 16: phone_sync must not expose local stats mutation shortcuts.
    {
        const auto before_focus_seconds = store.snapshot().today.focus_seconds;
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":16,"data":{"is_in_use":false,"app_name":"local_session_sync","duration_sec":1500}})"));
        assertSuccess(resp, 16);
        assert(resp["data"]["action_taken"] == false);
        assert(store.snapshot().today.focus_seconds == before_focus_seconds);
        std::cout << "  phone_sync rejects local stats side effects: OK\n";
    }

    // Test 17: get_console_data
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"get_console_data","id":17})"));
        assertSuccess(resp, 17);
        assert(resp["data"].contains("session"));
        assert(resp["data"].contains("device"));
        assert(resp["data"].contains("reminder"));
        assert(resp["data"].contains("frame_quality"));
        assert(resp["data"].contains("camera_preview"));
        assert(resp["data"].contains("today"));
        assert(resp["data"]["session"].contains("active"));
        assert(resp["data"]["session"].contains("session_id"));
        assert(resp["data"]["session"].contains("focus_state"));
        assert(resp["data"]["session"].contains("calibration_completed"));
        assert(resp["data"]["session"].contains("session_phase"));
        assert(resp["data"]["device"].contains("gimbal"));
        assert(resp["data"]["device"]["gimbal"].contains("connected"));
        assert(resp["data"]["device"]["gimbal"].contains("online"));
        assert(resp["data"]["device"]["gimbal"].contains("safe"));
        assert(resp["data"]["device"]["gimbal"].contains("xhome"));
        assert(resp["data"]["device"]["gimbal"].contains("yhome"));
        assert(resp["data"]["device"]["gimbal"].contains("calibrated"));
        assert(resp["data"]["device"]["gimbal"].contains("boost"));
        assert(resp["data"]["device"]["gimbal"].contains("motor"));
        assert(resp["data"]["device"]["gimbal"].contains("en"));
        assert(resp["data"]["device"]["gimbal"].contains("ix"));
        assert(resp["data"]["device"]["gimbal"].contains("iy"));
        assert(resp["data"]["device"]["gimbal"].contains("angle_x"));
        assert(resp["data"]["device"]["gimbal"].contains("angle_y"));
        assert(resp["data"]["device"]["gimbal"].contains("last_seen_ms"));
        assert(resp["data"]["device"]["gimbal"].contains("last_error"));
        std::cout << "  get_console_data: OK\n";
    }

    // Test 17b: get_gimbal_status
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"get_gimbal_status","id":170})"));
        assertSuccess(resp, 170);
        assert(resp["data"].contains("connected"));
        assert(resp["data"].contains("online"));
        assert(resp["data"].contains("safe"));
        assert(resp["data"].contains("xhome"));
        assert(resp["data"].contains("yhome"));
        assert(resp["data"].contains("calibrated"));
        assert(resp["data"].contains("boost"));
        assert(resp["data"].contains("motor"));
        assert(resp["data"].contains("en"));
        assert(resp["data"].contains("ix"));
        assert(resp["data"].contains("iy"));
        assert(resp["data"].contains("angle_x"));
        assert(resp["data"].contains("angle_y"));
        assert(resp["data"].contains("last_seen_ms"));
        assert(resp["data"].contains("last_error"));
        std::cout << "  get_gimbal_status: OK\n";
    }

    // Test 17c: gimbal nudge actions are exposed through the App control API.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"gimbal_control","id":172,"data":{"action":"nudge_left","step_deg":3}})"));
        assert(resp["status"] == "error");
        assert(resp["id"] == 172);
        const auto error = resp["error"].get<std::string>();
        assert(error.find("unavailable") != std::string::npos ||
               error.find("disabled") != std::string::npos);
        std::cout << "  gimbal_control nudge unavailable without monitor: OK\n";
    }

    // Test 17c.1: production feature gate also blocks legacy raw diagnostics.
    {
        SystemStateStore diag_store;
        SQLiteStore diag_sqlite(":memory:");
        AppContext diag_ctx;
        diag_sqlite.init(diag_ctx);
        DeviceManager diag_dev_mgr(diag_store);
        ReminderPolicy diag_policy(diag_store, diag_sqlite, diag_dev_mgr);
        diag_policy.init(diag_ctx);
        GimbalMonitor diag_gimbal_monitor(diag_store);
        BleGattServer diag_server(diag_store, diag_sqlite, diag_policy,
                                  diag_policy.getModeManager(), &diag_gimbal_monitor);
        diag_server.init(diag_ctx);

        DeviceStatus gimbal_ready = diag_store.snapshot().device;
        gimbal_ready.gimbal_online = true;
        gimbal_ready.gimbal_safe = true;
        diag_store.updateDeviceStatus(gimbal_ready);

        json diag_resp = parseResponse(diag_server.handleCommand(
            R"({"cmd":"gimbal_control","id":173,"data":{"action":"ping"}})"));
        assert(diag_resp["status"] == "error");
        assert(diag_resp["error"] == "Gimbal disabled");

        diag_server.shutdown(diag_ctx);
        diag_sqlite.shutdown(diag_ctx);
        std::cout << "  production gate blocks legacy gimbal diagnostics: OK\n";
    }

    // Test 17d: get_camera_preview_image returns the latest low-bandwidth snapshot container.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"get_camera_preview_image","id":171})"));
        assertSuccess(resp, 171);
        assert(resp["data"].contains("available"));
        assert(resp["data"].contains("width"));
        assert(resp["data"].contains("height"));
        assert(resp["data"].contains("pixel_format"));
        assert(resp["data"].contains("encoding"));
        assert(resp["data"].contains("data_base64"));
        assert(resp["data"].contains("quality"));
        std::cout << "  get_camera_preview_image: OK\n";
    }

    // Test 17e: upload_local_session stores by session_id idempotently.
    {
        json first = parseResponse(server.handleCommand(
            R"({"cmd":"upload_local_session","id":174,"data":{"session_id":"app_local_1","start_time_ms":1000,"end_time_ms":61000,"total_duration":60,"focus_duration":55,"distraction_count":2,"reminder_count":1,"phone_distraction_count":2,"source":"app_local"}})"));
        assertSuccess(first, 174);
        assert(first["data"]["stored"] == true);
        assert(first["data"]["already_exists"] == false);
        json second = parseResponse(server.handleCommand(
            R"({"cmd":"upload_local_session","id":175,"data":{"session_id":"app_local_1","start_time_ms":1000,"end_time_ms":61000,"total_duration":60,"focus_duration":55,"distraction_count":2,"reminder_count":1,"phone_distraction_count":2,"source":"app_local"}})"));
        assertSuccess(second, 175);
        assert(second["data"]["stored"] == true);
        assert(second["data"]["already_exists"] == true);
        std::cout << "  upload_local_session idempotent: OK\n";
    }

    // Test 17f: upload_local_session rejects invalid report payloads.
    {
        json reversed_time = parseResponse(server.handleCommand(
            R"({"cmd":"upload_local_session","id":176,"data":{"session_id":"bad_reversed","start_time_ms":61000,"end_time_ms":1000,"total_duration":60,"focus_duration":55,"distraction_count":0,"reminder_count":0,"phone_distraction_count":0}})"));
        assert(reversed_time["status"] == "error");

        json too_long = parseResponse(server.handleCommand(
            R"({"cmd":"upload_local_session","id":177,"data":{"session_id":"bad_long","start_time_ms":1000,"end_time_ms":90001000,"total_duration":90000,"focus_duration":10,"distraction_count":0,"reminder_count":0,"phone_distraction_count":0}})"));
        assert(too_long["status"] == "error");

        json bad_focus = parseResponse(server.handleCommand(
            R"({"cmd":"upload_local_session","id":178,"data":{"session_id":"bad_focus","start_time_ms":1000,"end_time_ms":61000,"total_duration":60,"focus_duration":61,"distraction_count":0,"reminder_count":0,"phone_distraction_count":0}})"));
        assert(bad_focus["status"] == "error");

        json bad_count = parseResponse(server.handleCommand(
            R"({"cmd":"upload_local_session","id":179,"data":{"session_id":"bad_count","start_time_ms":1000,"end_time_ms":61000,"total_duration":60,"focus_duration":55,"distraction_count":-1,"reminder_count":0,"phone_distraction_count":0}})"));
        assert(bad_count["status"] == "error");
        std::cout << "  upload_local_session invalid payloads rejected: OK\n";
    }

    // Test 18: session_feedback
    {
        json start_resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":18,"data":{"action":"start","target_duration_min":10}})"));
        std::string sid = start_resp["data"]["session_id"];

        json resp = parseResponse(server.handleCommand(
            "{\"cmd\":\"session_feedback\",\"id\":18,\"data\":{\"session_id\":\"" + sid + "\",\"feedback\":\"too_much\"}}"));
        assertSuccess(resp, 18);
        assert(resp["data"]["success"] == true);

        server.handleCommand(R"({"cmd":"session_control","id":18,"data":{"action":"end"}})");
        std::cout << "  session_feedback: OK\n";
    }

    // Test 19: get_session_history
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"get_session_history","id":19,"data":{"limit":1}})"));
        assertSuccess(resp, 19);
        assert(resp["data"].contains("sessions"));
        assert(resp["data"].contains("has_more"));
        assert(resp["data"].contains("next_cursor"));
        std::cout << "  get_session_history: OK\n";
    }

    // Test 19b: get_session_history clamps untrusted client limits.
    {
        SystemStateStore hist_store;
        SQLiteStore hist_sqlite(":memory:");
        AppContext hist_ctx;
        hist_sqlite.init(hist_ctx);
        DeviceManager hist_dev_mgr(hist_store);
        ReminderPolicy hist_policy(hist_store, hist_sqlite, hist_dev_mgr);
        hist_policy.init(hist_ctx);
        BleGattServer hist_server(hist_store, hist_sqlite, hist_policy,
                                  hist_policy.getModeManager());
        hist_server.init(hist_ctx);

        for (int i = 0; i < 105; ++i) {
            const int id = 1900 + i;
            const int64_t start_ms = 1000LL + static_cast<int64_t>(i) * 100000LL;
            const int64_t end_ms = start_ms + 60000LL;
            std::string payload = std::string("{\"cmd\":\"upload_local_session\",\"id\":") +
                std::to_string(id) +
                ",\"data\":{\"session_id\":\"hist_" + std::to_string(i) +
                "\",\"start_time_ms\":" + std::to_string(start_ms) +
                ",\"end_time_ms\":" + std::to_string(end_ms) +
                ",\"total_duration\":60,\"focus_duration\":60,\"distraction_count\":0,\"reminder_count\":0,\"phone_distraction_count\":0}}";
            json upload = parseResponse(hist_server.handleCommand(payload));
            assertSuccess(upload, id);
        }

        json resp = parseResponse(hist_server.handleCommand(
            R"({"cmd":"get_session_history","id":2000,"data":{"limit":1000}})"));
        assertSuccess(resp, 2000);
        assert(resp["data"]["sessions"].is_array());
        assert(resp["data"]["sessions"].size() == 100);
        hist_server.shutdown(hist_ctx);
        hist_sqlite.shutdown(hist_ctx);
        std::cout << "  get_session_history limit clamp: OK\n";
    }

    // Test 20: session_feedback (invalid value)
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_feedback","id":20,"data":{"session_id":"session_bad","feedback":"invalid"}})"));
        assert(resp["status"] == "error");
        std::cout << "  session_feedback (invalid): OK\n";
    }

    // Test 21: session_feedback (missing session_id session)
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_feedback","id":21,"data":{"session_id":"non_exist_session_123","feedback":"too_little"}})"));
        assert(resp["status"] == "error");
        assert(resp["error"].get<std::string>().find("Session not found") != std::string::npos);
        std::cout << "  session_feedback (not found): OK\n";
    }

    // Test 22: start/end/report/history/feedback use same session_id (closed loop)
    {
        json start_resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":22,"data":{"action":"start","target_duration_min":10}})"));
        assertSuccess(start_resp, 22);
        assert(start_resp["data"]["success"] == true);
        std::string sid = start_resp["data"]["session_id"];

        json end_resp = parseResponse(server.handleCommand(
            R"({"cmd":"session_control","id":23,"data":{"action":"end"}})"));
        assertSuccess(end_resp, 23);
        auto session_id = end_resp["data"]["session_id"].get<std::string>();
        assert(session_id == sid);

        json last_report = parseResponse(server.handleCommand(
            R"({"cmd":"get_last_report","id":24})"));
        assertSuccess(last_report, 24);
        assert(last_report["data"]["session_id"] == sid);

        json history = parseResponse(server.handleCommand(
            R"({"cmd":"get_session_history","id":25,"data":{"limit":1}})"));
        assertSuccess(history, 25);
        assert(history["data"]["sessions"].is_array());
        assert(!history["data"]["sessions"].empty());
        assert(history["data"]["sessions"][0]["session_id"] == sid);

        std::string fb_payload = std::string("{\"cmd\":\"session_feedback\",\"id\":26,\"data\":{\"session_id\":\"") + sid
                                     + "\",\"feedback\":\"too_much\"}}";
        json fb_resp = parseResponse(server.handleCommand(fb_payload));
        assertSuccess(fb_resp, 26);
        assert(fb_resp["data"]["success"] == true);

        std::cout << "  session lifecycle/session_feedback closed loop: OK\n";
    }

    // Test 23: Unknown command
    {
        response = parseResponse(server.handleCommand(R"({"cmd":"unknown_cmd","id":99})"));
        assert(response["status"] == "error");
        assert(response["id"] == 99);
        std::cout << "  unknown command: OK\n";
    }

    // Test 23b: oversized BLE requests fail explicitly instead of being parsed silently.
    {
        std::string huge = R"({"cmd":"get_current_state","id":9900,"data":{"blob":")" +
            std::string(9000, 'x') + R"("}})";
        response = parseResponse(server.handleCommand(huge));
        assert(response["status"] == "error");
        assert(response["error"].get<std::string>().find("too large") != std::string::npos);
        std::cout << "  oversized BLE request rejected: OK\n";
    }

    // Test 24: capable Apps negotiate reliable critical pushes and ACK them idempotently.
    {
        server.setPhoneConnected(true);
        json hello = parseResponse(server.handleCommand(
            R"({"cmd":"client_hello","id":240,"data":{"capabilities":["event_ack_v1","event_dedupe_v1"]}})"));
        assertSuccess(hello, 240);
        assert(hello["data"]["reliable_push"] == true);
        assert(std::find(hello["data"]["server_capabilities"].begin(),
                         hello["data"]["server_capabilities"].end(),
                         "external_power_v1") != hello["data"]["server_capabilities"].end());
        assert(std::find(hello["data"]["server_capabilities"].begin(),
                         hello["data"]["server_capabilities"].end(),
                         "power_profile_override_v1") != hello["data"]["server_capabilities"].end());
        assert(std::find(hello["data"]["server_capabilities"].begin(),
                         hello["data"]["server_capabilities"].end(),
                         "band_proxy_control_v1") != hello["data"]["server_capabilities"].end());

        auto device = store.snapshot().device;
        device.ble_enabled = true;
        store.updateDeviceStatus(device);
        dev_mgr.runOnce(ctx);
        json band_search = parseResponse(server.handleCommand(
            R"({"cmd":"band_control","id":2401,"data":{"action":"scan_connect"}})"));
        assertSuccess(band_search, 2401);
        assert(band_search["data"]["accepted"] == true);
        assert(band_search["data"]["operation"] == "scan_connect");

        store.pushEvent(R"({"cmd":"push_alert","data":{"session_id":"ack_test","level":2,"phone_actions":["SCREEN","SOUND"]}})");
        json current = parseResponse(server.handleCommand(
            R"({"cmd":"get_current_state","id":241})"));
        assertSuccess(current, 241);
        assert(current["data"].contains("event_delivery"));
        const auto& events = current["data"]["event_delivery"]["events"];
        assert(!events.empty());
        std::string event_id;
        for (const auto& event : events) {
            if (event.value("session_id", "") == "ack_test") {
                event_id = event["event_id"].get<std::string>();
            }
        }
        assert(!event_id.empty());

        const std::string ack_payload =
            std::string("{\"cmd\":\"event_ack\",\"id\":242,\"data\":{\"event_id\":\"") +
            event_id + "\",\"session_id\":\"ack_test\",\"stage\":\"received\"}}";
        json ack = parseResponse(server.handleCommand(ack_payload));
        assertSuccess(ack, 242);
        assert(ack["data"]["ack_status"] == "accepted");
        assert((store.snapshot().device.last_delivered_channels_mask &
                kNotificationChannelScreenMask) != 0);
        assert((store.snapshot().device.last_delivered_channels_mask &
                kNotificationChannelSoundMask) != 0);

        json duplicate = parseResponse(server.handleCommand(ack_payload));
        assertSuccess(duplicate, 242);
        assert(duplicate["data"]["ack_status"] == "duplicate");
        server.setPhoneConnected(false);
        std::cout << "  reliable critical push ACK: OK\n";
    }

    // Test 22: push callback registration
    {
        bool callback_called = false;
        store.setPushCallback([&callback_called](const std::string&) {
            callback_called = true;
        });
        store.pushEvent(R"({"cmd":"push_state","data":{"focus_state":0}})");
        assert(callback_called);
        std::cout << "  push callback: OK\n";
    }

    server.shutdown(ctx);
    dev_mgr.shutdown(ctx);
    std::cout << "All BLE GATT Server tests passed!\n";
    return 0;
}
