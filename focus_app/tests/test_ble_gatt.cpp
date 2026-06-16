#include "network/BleGattServer.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "storage/ReportGenerator.h"
#include "policy/ReminderPolicy.h"
#include "device/DeviceManager.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace edgefocus;

int main() {
    std::cout << "Starting BLE GATT Server tests...\n";

    SystemStateStore store;
    SQLiteStore sqlite_store(":memory:");
    AppContext ctx;
    sqlite_store.init(ctx);

    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    policy.init(ctx);
    ReportGenerator report_gen(sqlite_store);
    BleGattServer server(store, sqlite_store, policy, policy.getModeManager(), report_gen);
    server.init(ctx);

    // Test 1: get_device_status
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_device_status\"}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("\"battery_percent\"") != std::string::npos);
        assert(resp.find("\"system_mode\"") != std::string::npos);
        std::cout << "  get_device_status: OK\n";
    }

    // Test 2: get_current_state
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_current_state\"}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("\"focus_state\"") != std::string::npos);
        assert(resp.find("\"elapsed_ms\"") != std::string::npos);
        std::cout << "  get_current_state: OK\n";
    }

    // Test 3: get_today_summary
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_today_summary\"}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("\"total_seconds\"") != std::string::npos);
        assert(resp.find("\"focus_seconds\"") != std::string::npos);
        std::cout << "  get_today_summary: OK\n";
    }

    // Test 4: get_last_report (no report yet)
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_last_report\"}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("null") != std::string::npos);
        std::cout << "  get_last_report (empty): OK\n";
    }

    // Test 5: session_control start
    {
        std::string resp = server.handleCommand("{\"cmd\":\"session_control\",\"action\":\"start\",\"target_duration_min\":25}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("\"session_id\"") != std::string::npos);
        auto snap = store.snapshot();
        assert(snap.session.active == true);
        assert(snap.session.target_duration_ms == 25 * 60000);
        std::cout << "  session_control start: OK\n";
    }

    // Test 6: get_current_state (active session)
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_current_state\"}");
        assert(resp.find("\"target_duration_ms\":1500000") != std::string::npos);
        std::cout << "  get_current_state (active): OK\n";
    }

    // Test 7: session_control end
    {
        std::string resp = server.handleCommand("{\"cmd\":\"session_control\",\"action\":\"end\"}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        auto snap = store.snapshot();
        assert(snap.session.active == false);
        std::cout << "  session_control end: OK\n";
    }

    // Test 8: update_preferences
    {
        std::string resp = server.handleCommand("{\"cmd\":\"update_preferences\",\"system_mode\":1}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("\"system_mode\":1") != std::string::npos);
        assert(policy.getModeManager().getMode() == SystemMode::SILENT);
        std::cout << "  update_preferences: OK\n";
    }

    // Test 9: reminder_ack
    {
        std::string resp = server.handleCommand("{\"cmd\":\"reminder_ack\"}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("\"cooling_ms\"") != std::string::npos);
        std::cout << "  reminder_ack: OK\n";
    }

    // Test 10: phone_sync
    {
        std::string resp = server.handleCommand("{\"cmd\":\"phone_sync\",\"is_in_use\":true,\"distraction_level\":2}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("\"action_taken\":\"strong_distraction\"") != std::string::npos);
        std::cout << "  phone_sync: OK\n";
    }

    // Test 11: get_console_data
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_console_data\"}");
        assert(resp.find("\"status\":\"ok\"") != std::string::npos);
        assert(resp.find("\"session\"") != std::string::npos);
        assert(resp.find("\"device\"") != std::string::npos);
        assert(resp.find("\"reminder\"") != std::string::npos);
        assert(resp.find("\"today\"") != std::string::npos);
        std::cout << "  get_console_data: OK\n";
    }

    // Test 12: Unknown command
    {
        std::string resp = server.handleCommand("{\"cmd\":\"unknown_cmd\"}");
        assert(resp.find("\"status\":\"error\"") != std::string::npos);
        std::cout << "  unknown command: OK\n";
    }

    server.shutdown(ctx);
    std::cout << "All BLE GATT Server tests passed!\n";
    return 0;
}
