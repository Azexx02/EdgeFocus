#include "network/BleGattServer.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "policy/ReminderPolicy.h"
#include "device/DeviceManager.h"
#include <iostream>
#include <cassert>
#include <string>
#include <nlohmann/json.hpp>

using namespace edgefocus;
using json = nlohmann::json;

json parseResponse(const std::string& resp) {
    return json::parse(resp);
}

void assertSuccess(const json& resp, int expected_id) {
    assert(resp["status"] == "success");
    assert(resp["id"] == expected_id);
}

int main() {
    std::cout << "Starting BLE GATT Server tests...\n";
    json response;

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

    SystemStateStore store;
    SQLiteStore sqlite_store(":memory:");
    AppContext ctx;
    sqlite_store.init(ctx);

    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    policy.init(ctx);
    BleGattServer server(store, sqlite_store, policy, policy.getModeManager());
    server.init(ctx);

    // Test 1: get_device_status
    {
        json resp = parseResponse(server.handleCommand(R"({"cmd":"get_device_status","id":1})"));
        assertSuccess(resp, 1);
        assert(resp["data"].contains("battery_percent"));
        assert(resp["data"].contains("battery_voltage_mv"));
        assert(resp["data"].contains("battery_charging"));
        assert(resp["data"].contains("power_source"));
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
        std::cout << "  get_current_state: OK\n";
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
        auto snap = store.snapshot();
        assert(snap.session.active == true);
        assert(snap.session.target_duration_ms == 25 * 60000);
        assert(snap.session.start_ms > 0);
        assert(snap.session.start_ms <= snap.snapshot_ms);

        json precheck_state = parseResponse(server.handleCommand(
            R"({"cmd":"get_current_state","id":49})"));
        assertSuccess(precheck_state, 49);
        assert(precheck_state["data"]["session_phase"] == "precheck");

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

    // Test 11a: defaults are SCREEN+BAND+SOUND / SCREEN+BAND.
    {
        json resp = parseResponse(server.handleCommand(R"({"cmd":"get_preferences","id":1110})"));
        assertSuccess(resp, 1110);
        assert(resp["data"]["normal_channels_mask"] == 7);
        assert(resp["data"]["silent_channels_mask"] == 3);
        assert(resp["data"]["sound_volume"] == 0.7f);
        assert(resp["data"]["board_output_route"] == "board");
        assert(resp["data"]["board_output_disabled"] == false);
        assert(resp["data"]["screen_output_route"] == "board");
        assert(resp["data"]["sound_output_route"] == "board");
        assert(resp["data"]["gimbal_enabled"] == true);
        assert(resp["data"]["normal_channels"] == json::array({"SCREEN", "BAND", "SOUND"}));
        assert(resp["data"]["silent_channels"] == json::array({"SCREEN", "BAND"}));
        std::cout << "  get_preferences defaults: OK\n";
    }

    // Test 11: update_preferences
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":11,"data":{"system_mode":1,"default_focus_duration_min":35,"sound_volume":0.25,"normal_channels_mask":3,"silent_channels_mask":2}})"));
        assertSuccess(resp, 11);
        assert(resp["data"]["system_mode"] == 1);
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

    // Test 11f: update_preferences stores board/phone output routes.
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"update_preferences","id":115,"data":{"board_output_route":"phone","gimbal_enabled":false}})"));
        assertSuccess(resp, 115);
        assert(resp["data"]["board_output_route"] == "phone");
        assert(resp["data"]["board_output_disabled"] == true);
        assert(resp["data"]["screen_output_route"] == "phone");
        assert(resp["data"]["sound_output_route"] == "phone");
        assert(resp["data"]["gimbal_enabled"] == false);
    }

    // Test 12: get_preferences
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"get_preferences","id":12})"));
        assertSuccess(resp, 12);
        assert(resp["data"].contains("system_mode"));
        assert(resp["data"].contains("vibration_strength"));
        assert(resp["data"].contains("sound_volume"));
        assert(resp["data"].contains("led_brightness"));
        assert(resp["data"].contains("reminder_sensitivity"));
        assert(resp["data"].contains("away_timeout_min"));
        assert(resp["data"].contains("default_focus_duration_min"));
        assert(resp["data"]["default_focus_duration_min"] == 35);
        assert(resp["data"].contains("normal_channels_mask"));
        assert(resp["data"].contains("silent_channels_mask"));
        assert(resp["data"].contains("normal_channels"));
        assert(resp["data"].contains("silent_channels"));
        assert(resp["data"]["board_output_route"] == "phone");
        assert(resp["data"]["board_output_disabled"] == true);
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

    // Test 13: reminder_ack
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"reminder_ack","id":13,"data":{}})"));
        assertSuccess(resp, 13);
        assert(resp["data"].contains("cooling_ms"));
        std::cout << "  reminder_ack: OK\n";
    }

    // Test 14: phone_sync (in use)
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
        assert(sig.usage_state == PhoneUsageState::IN_USE);
        std::cout << "  phone_sync (in use): OK\n";
    }

    // Test 14b: phone_sync keeps usage state separate from distraction level
    {
        json resp_state = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":114,"data":{"is_in_use":true,"distraction_level":3,"app_name":"unknown"}})"));
        assertSuccess(resp_state, 114);
        PhoneUsageSignal sig;
        assert(store.consumePhoneSignal(sig));
        assert(sig.active == true);
        assert(sig.distraction_level == 3);
        assert(sig.usage_state == PhoneUsageState::IN_USE);

        json resp_level = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":115,"data":{"is_in_use":true,"distraction_level":1,"app_category":"entertainment","app_name":"video"}})"));
        assertSuccess(resp_level, 115);
        assert(store.consumePhoneSignal(sig));
        assert(sig.active == true);
        assert(sig.distraction_level == 1);
        assert(sig.usage_state == PhoneUsageState::PLAYING);
        std::cout << "  phone_sync (state/level separated): OK\n";
    }

    // Test 15: phone_sync (not in use)
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":15,"data":{"is_in_use":false,"distraction_level":0}})"));
        assertSuccess(resp, 15);
        assert(resp["data"]["action_taken"] == false);
        std::cout << "  phone_sync (not in use): OK\n";
    }

    // Test 16: phone_sync (local_session_sync)
    {
        json resp = parseResponse(server.handleCommand(
            R"({"cmd":"phone_sync","id":16,"data":{"is_in_use":false,"app_name":"local_session_sync","duration_sec":1500}})"));
        assertSuccess(resp, 16);
        std::cout << "  phone_sync (local_session_sync): OK\n";
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
    std::cout << "All BLE GATT Server tests passed!\n";
    return 0;
}
