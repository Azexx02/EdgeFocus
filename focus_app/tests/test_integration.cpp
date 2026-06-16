/**
 * Integration test: Verify module-to-module data contracts.
 * 
 * Tests the data flow between modules to ensure input/output match:
 * 1. BehaviorEngine → SystemStateStore → ReminderPolicy (FocusState)
 * 2. ReminderPolicy → SystemStateStore → DeviceManager (InterventionCommand)
 * 3. StatusPage(session) → BehaviorEngine (SessionInfo fields)
 * 4. BehaviorEngine → SystemStateStore → BleGattServer (all snapshot fields)
 * 5. SettingsPage → ModeManager → ReminderPolicy (SystemMode)
 * 6. ReportGenerator → SystemStateStore → ReviewPage (SessionReport fields)
 */
#include "app/SystemStateStore.h"
#include "app/AppContext.h"
#include "storage/SQLiteStore.h"
#include "storage/ReportGenerator.h"
#include "behavior/FocusStateMachine.h"
#include "behavior/BehaviorEngine.h"
#include "policy/ReminderPolicy.h"
#include "policy/ModeManager.h"
#include "device/DeviceManager.h"
#include "network/BleGattServer.h"
#include "vision/VisionResult.h"
#include <iostream>
#include <cassert>
#include <string>
#include <sstream>

using namespace edgefocus;

int tests_passed = 0;
int tests_failed = 0;

#define CHECK(expr, msg) do { \
    if (!(expr)) { \
        std::cerr << "  FAIL: " << msg << " (" #expr ")" << std::endl; \
        tests_failed++; \
    } else { \
        std::cout << "  PASS: " << msg << std::endl; \
        tests_passed++; \
    } \
} while(0)

// ============================================================================
// Test 1: BehaviorEngine → ReminderPolicy (FocusState contract)
// ============================================================================
void test_behavior_to_policy_contract() {
    std::cout << "\n[Test 1] BehaviorEngine → ReminderPolicy (FocusState)\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite, dev_mgr);
    policy.init(ctx);

    // Simulate BehaviorEngine writing FocusState to store
    // Then verify ReminderPolicy reads it correctly

    // FOCUSED → no intervention
    store.updateFocusState(FocusState::FOCUSED, 1000, "test");
    auto snap = store.snapshot();
    CHECK(snap.focus_state == FocusState::FOCUSED, "Store reads FOCUSED correctly");
    auto cmd = policy.processState(snap.focus_state, 2000);
    CHECK(!cmd.has_value(), "FOCUSED → no intervention from policy");

    // LOOKING_AWAY → LEVEL_1
    store.updateFocusState(FocusState::LOOKING_AWAY, 3000, "test");
    snap = store.snapshot();
    CHECK(snap.focus_state == FocusState::LOOKING_AWAY, "Store reads LOOKING_AWAY correctly");
    cmd = policy.processState(snap.focus_state, 4000);
    CHECK(cmd.has_value(), "LOOKING_AWAY → policy generates command");
    CHECK(cmd->level == CommandLevel::LEVEL_1, "LOOKING_AWAY → LEVEL_1");

    // PHONE_IN_USE → LEVEL_2
    store.updateFocusState(FocusState::PHONE_IN_USE, 5000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 12000);
    CHECK(cmd.has_value(), "PHONE_IN_USE → policy generates command");
    CHECK(cmd->level == CommandLevel::LEVEL_2, "PHONE_IN_USE → LEVEL_2");

    // PHONE_IN_LEARNING → no intervention
    store.updateFocusState(FocusState::PHONE_IN_LEARNING, 13000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 20000);
    CHECK(!cmd.has_value(), "PHONE_IN_LEARNING → no intervention");

    // PHONE_IN_PLAYING → LEVEL_3
    store.updateFocusState(FocusState::PHONE_IN_PLAYING, 21000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 28000);
    CHECK(cmd.has_value(), "PHONE_IN_PLAYING → policy generates command");
    CHECK(cmd->level == CommandLevel::LEVEL_3, "PHONE_IN_PLAYING → LEVEL_3");

    // SLEEPY_OR_SLUMPED → REST_SUGGESTION
    store.updateFocusState(FocusState::SLEEPY_OR_SLUMPED, 29000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 36000);
    CHECK(cmd.has_value(), "SLEEPY → policy generates command");
    CHECK(cmd->level == CommandLevel::REST_SUGGESTION, "SLEEPY → REST_SUGGESTION");
}

// ============================================================================
// Test 2: ReminderPolicy → DeviceManager (InterventionCommand contract)
// ============================================================================
void test_policy_to_device_contract() {
    std::cout << "\n[Test 2] ReminderPolicy → DeviceManager (InterventionCommand)\n";

    SystemStateStore store;

    // Verify InterventionCommand struct fields match between producer and consumer
    InterventionCommand cmd;
    cmd.level = CommandLevel::LEVEL_2;
    cmd.message = "test message";
    cmd.timestamp_ms = 12345;

    CHECK(cmd.level == CommandLevel::LEVEL_2, "CommandLevel field matches");
    CHECK(cmd.message == "test message", "message field matches");
    CHECK(cmd.timestamp_ms == 12345, "timestamp_ms field matches");

    // Verify CommandLevel enum values are consistent
    CHECK(static_cast<int>(CommandLevel::NONE) == 0, "NONE=0");
    CHECK(static_cast<int>(CommandLevel::LEVEL_1) == 1, "LEVEL_1=1");
    CHECK(static_cast<int>(CommandLevel::LEVEL_2) == 2, "LEVEL_2=2");
    CHECK(static_cast<int>(CommandLevel::LEVEL_3) == 3, "LEVEL_3=3");
    CHECK(static_cast<int>(CommandLevel::REST_SUGGESTION) == 4, "REST_SUGGESTION=4");
}

// ============================================================================
// Test 3: StatusPage session creation → BehaviorEngine reads
// ============================================================================
void test_session_info_contract() {
    std::cout << "\n[Test 3] SessionInfo contract (StatusPage → BehaviorEngine)\n";

    SystemStateStore store;

    // Simulate StatusPage creating a session
    SessionInfo s;
    s.active = true;
    s.calibrating = true;
    s.calibration_completed = false;
    s.session_id = "session_12345";
    s.start_ms = 100000;
    s.elapsed_ms = 0;
    s.distraction_count = 0;
    s.reminder_count = 0;
    s.away_since_ms = 0;
    s.away_timeout_ms = 30 * 60 * 1000;
    s.target_duration_ms = 25 * 60 * 1000;
    s.remaining_ms = 0;
    store.updateSession(s);

    // Verify BehaviorEngine can read what it needs
    auto snap = store.snapshot();
    CHECK(snap.session.active == true, "active=true");
    CHECK(snap.session.calibrating == true, "calibrating=true");
    CHECK(snap.session.calibration_completed == false, "calibration_completed=false");
    CHECK(snap.session.session_id == "session_12345", "session_id preserved");
    CHECK(snap.session.start_ms == 100000, "start_ms preserved");
    CHECK(snap.session.target_duration_ms == 25 * 60 * 1000, "target_duration_ms=25min");
    CHECK(snap.session.away_timeout_ms == 30 * 60 * 1000, "away_timeout_ms=30min");

    // Simulate BehaviorEngine updating session after calibration
    SessionInfo s2 = snap.session;
    s2.calibrating = false;
    s2.calibration_completed = true;
    s2.elapsed_ms = 5000;
    store.updateSession(s2);

    snap = store.snapshot();
    CHECK(snap.session.calibration_completed == true, "calibration_completed updated");
    CHECK(snap.session.elapsed_ms == 5000, "elapsed_ms updated");

    // Simulate timer: remaining_ms = target - elapsed
    s2 = snap.session;
    s2.remaining_ms = s2.target_duration_ms - s2.elapsed_ms;
    store.updateSession(s2);
    snap = store.snapshot();
    CHECK(snap.session.remaining_ms == 25 * 60 * 1000 - 5000, "remaining_ms = target - elapsed");
}

// ============================================================================
// Test 4: SystemStateStore → BleGattServer (all snapshot fields)
// ============================================================================
void test_snapshot_to_gatt_contract() {
    std::cout << "\n[Test 4] SystemStateStore → BleGattServer (snapshot fields)\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite, dev_mgr);
    policy.init(ctx);
    ReportGenerator report_gen(sqlite);
    BleGattServer server(store, sqlite, policy, policy.getModeManager(), report_gen);
    server.init(ctx);

    // Set up some state
    store.updateFocusState(FocusState::PHONE_IN_USE, 1000, "phone detected");
    SessionInfo s;
    s.active = true;
    s.session_id = "test_session";
    s.start_ms = 50000;
    s.elapsed_ms = 30000;
    s.target_duration_ms = 1500000;
    s.remaining_ms = 1470000;
    s.distraction_count = 3;
    s.reminder_count = 2;
    store.updateSession(s);

    TodayStats today;
    today.total_seconds = 3600;
    today.focus_seconds = 2400;
    today.sessions = 2;
    today.events = 5;
    today.phone_distraction_count = 1;
    store.updateTodayStats(today);

    DeviceStatus dev;
    dev.system_mode = SystemMode::SILENT;
    dev.camera_ok = true;
    dev.bracelet_connected = true;
    dev.battery_percent = 85;
    store.updateDeviceStatus(dev);

    store.updateReminder(CommandLevel::LEVEL_2, "test reminder", 1000);
    store.updateFrameQuality(0.8f, "good", false);

    // Now verify BleGattServer reads all fields correctly
    // get_device_status
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_device_status\"}");
        CHECK(resp.find("\"battery_percent\":85") != std::string::npos, "battery=85");
        CHECK(resp.find("\"system_mode\":1") != std::string::npos, "system_mode=1 (SILENT)");
        CHECK(resp.find("\"camera_ok\":true") != std::string::npos, "camera_ok=true");
        CHECK(resp.find("\"bracelet_connected\":true") != std::string::npos, "bracelet_connected=true");
        CHECK(resp.find("\"is_session_active\":true") != std::string::npos, "is_session_active=true");
    }

    // get_current_state
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_current_state\"}");
        CHECK(resp.find("\"focus_state\":3") != std::string::npos, "focus_state=3 (PHONE_IN_USE)");
        CHECK(resp.find("\"elapsed_ms\":30000") != std::string::npos, "elapsed_ms=30000");
        CHECK(resp.find("\"target_duration_ms\":1500000") != std::string::npos, "target_duration_ms=1500000");
        CHECK(resp.find("\"remaining_ms\":1470000") != std::string::npos, "remaining_ms=1470000");
        CHECK(resp.find("\"distraction_count\":3") != std::string::npos, "distraction_count=3");
        CHECK(resp.find("\"reminder_count\":2") != std::string::npos, "reminder_count=2");
    }

    // get_today_summary
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_today_summary\"}");
        CHECK(resp.find("\"total_seconds\":3600") != std::string::npos, "total_seconds=3600");
        CHECK(resp.find("\"focus_seconds\":2400") != std::string::npos, "focus_seconds=2400");
        CHECK(resp.find("\"sessions\":2") != std::string::npos, "sessions=2");
        CHECK(resp.find("\"events\":5") != std::string::npos, "events=5");
        CHECK(resp.find("\"phone_distraction_count\":1") != std::string::npos, "phone_distraction_count=1");
    }

    // get_console_data
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_console_data\"}");
        CHECK(resp.find("\"session\"") != std::string::npos, "has session");
        CHECK(resp.find("\"device\"") != std::string::npos, "has device");
        CHECK(resp.find("\"reminder\"") != std::string::npos, "has reminder");
        CHECK(resp.find("\"today\"") != std::string::npos, "has today");
        CHECK(resp.find("\"frame_quality\"") != std::string::npos, "has frame_quality");
    }

    server.shutdown(ctx);
}

// ============================================================================
// Test 5: SettingsPage → ModeManager → ReminderPolicy
// ============================================================================
void test_mode_sync_contract() {
    std::cout << "\n[Test 5] SettingsPage → ModeManager → ReminderPolicy\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite, dev_mgr);
    policy.init(ctx);

    ModeManager& mm = policy.getModeManager();

    // Simulate SettingsPage toggling mode
    CHECK(mm.getMode() == SystemMode::NORMAL, "Initial mode=NORMAL");

    mm.setMode(SystemMode::SILENT);
    CHECK(mm.getMode() == SystemMode::SILENT, "Mode changed to SILENT");

    // Verify ReminderPolicy reads the updated mode
    // SILENT should downgrade LEVEL_2 to LEVEL_1
    store.updateFocusState(FocusState::PHONE_IN_USE, 1000, "test");
    auto cmd = policy.processState(FocusState::PHONE_IN_USE, 8000);
    CHECK(cmd.has_value(), "PHONE_IN_USE generates command");
    CHECK(cmd->level == CommandLevel::LEVEL_1, "SILENT downgrades LEVEL_2→LEVEL_1");
    CHECK(cmd->message.find("[Silent]") != std::string::npos, "Message has [Silent] prefix");

    mm.setMode(SystemMode::NORMAL);
    cmd = policy.processState(FocusState::PHONE_IN_USE, 16000);
    CHECK(cmd.has_value(), "PHONE_IN_USE generates command");
    CHECK(cmd->level == CommandLevel::LEVEL_2, "NORMAL keeps LEVEL_2");

    // Also verify store is updated (for UI display)
    DeviceStatus ds = store.snapshot().device;
    ds.system_mode = mm.getMode();
    store.updateDeviceStatus(ds);
    CHECK(store.snapshot().device.system_mode == SystemMode::NORMAL, "Store reflects mode");
}

// ============================================================================
// Test 6: ReportGenerator → SystemStateStore → ReviewPage
// ============================================================================
void test_report_contract() {
    std::cout << "\n[Test 6] ReportGenerator → SystemStateStore → ReviewPage\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    // Simulate a session with events
    sqlite.logEvent({StorageEventType::SessionStart, "report_test", 1000, ""});
    sqlite.logEvent({StorageEventType::StateChange, "report_test", 5000, "Changed to 0"}); // FOCUSED
    sqlite.logEvent({StorageEventType::StateChange, "report_test", 15000, "Changed to 3"}); // PHONE
    sqlite.logEvent({StorageEventType::StateChange, "report_test", 20000, "Changed to 0"}); // FOCUSED
    sqlite.logEvent({StorageEventType::ReminderIssued, "report_test", 16000, "Level: 2"});
    sqlite.logEvent({StorageEventType::SessionEnd, "report_test", 30000, ""});

    for (int i = 0; i < 10; ++i) sqlite.runOnce(ctx);

    // Generate report
    ReportGenerator rg(sqlite);
    SessionReport report = rg.generateReport("report_test");

    // Verify report fields
    CHECK(report.session_id == "report_test", "session_id matches");
    CHECK(report.total_duration == 29, "total_duration=29s (30000-1000)/1000");
    CHECK(report.distraction_count >= 1, "distraction_count >= 1");
    CHECK(report.reminder_count == 1, "reminder_count=1");
    CHECK(report.focus_duration >= 0, "focus_duration >= 0");
    CHECK(report.longest_focus_streak_ms >= 0, "longest_focus_streak_ms >= 0");

    // Store report
    store.updateLastReport(report);

    // Verify ReviewPage can read it
    auto snap = store.snapshot();
    CHECK(snap.last_report.session_id == "report_test", "ReviewPage reads session_id");
    CHECK(snap.last_report.total_duration == 29, "ReviewPage reads total_duration");
    CHECK(snap.last_report.reminder_count == 1, "ReviewPage reads reminder_count");

    // Verify score calculation (what ReviewPage does)
    int score = 0;
    if (snap.last_report.total_duration > 0) {
        float ratio = static_cast<float>(snap.last_report.focus_duration) / 
                      static_cast<float>(snap.last_report.total_duration);
        score = static_cast<int>(ratio * 100);
    }
    CHECK(score >= 0 && score <= 100, "Focus score is valid percentage");
}

// ============================================================================
// Test 7: Session lifecycle end-to-end
// ============================================================================
void test_session_lifecycle() {
    std::cout << "\n[Test 7] Session lifecycle (start → focus → end → report)\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite, dev_mgr);
    policy.init(ctx);
    ReportGenerator rg(sqlite);
    BleGattServer server(store, sqlite, policy, policy.getModeManager(), rg);
    server.init(ctx);

    // 1. Start session via BLE
    std::string resp = server.handleCommand(
        "{\"cmd\":\"session_control\",\"action\":\"start\",\"target_duration_min\":25}");
    CHECK(resp.find("\"status\":\"ok\"") != std::string::npos, "Session started via BLE");

    auto snap = store.snapshot();
    CHECK(snap.session.active == true, "Session is active");
    CHECK(snap.session.target_duration_ms == 25 * 60000, "Target=25min");
    CHECK(!snap.session.session_id.empty(), "session_id generated");

    // 2. Simulate BehaviorEngine processing
    std::string sid = snap.session.session_id;
    SessionInfo s = snap.session;
    s.calibrating = false;
    s.calibration_completed = true;
    s.elapsed_ms = 60000;
    s.distraction_count = 2;
    store.updateSession(s);
    store.updateFocusState(FocusState::FOCUSED, 60000, "focused");

    // Log some events for report
    sqlite.logEvent({StorageEventType::SessionStart, sid, 0, ""});
    sqlite.logEvent({StorageEventType::StateChange, sid, 30000, "Changed to 0"});
    sqlite.logEvent({StorageEventType::StateChange, sid, 45000, "Changed to 3"});
    sqlite.logEvent({StorageEventType::StateChange, sid, 55000, "Changed to 0"});
    sqlite.logEvent({StorageEventType::SessionEnd, sid, 60000, ""});
    for (int i = 0; i < 10; ++i) sqlite.runOnce(ctx);

    // 3. End session via BLE
    resp = server.handleCommand("{\"cmd\":\"session_control\",\"action\":\"end\"}");
    CHECK(resp.find("\"status\":\"ok\"") != std::string::npos, "Session ended via BLE");

    snap = store.snapshot();
    CHECK(snap.session.active == false, "Session is inactive after end");

    // 4. Verify report was generated
    resp = server.handleCommand("{\"cmd\":\"get_last_report\"}");
    CHECK(resp.find("\"total_duration\"") != std::string::npos, "Report has total_duration");
    CHECK(resp.find("\"focus_duration\"") != std::string::npos, "Report has focus_duration");
    CHECK(resp.find("\"distraction_count\"") != std::string::npos, "Report has distraction_count");

    server.shutdown(ctx);
}

// ============================================================================
// Test 8: FocusState enum values match across all consumers
// ============================================================================
void test_focus_state_enum_consistency() {
    std::cout << "\n[Test 8] FocusState enum consistency\n";

    // Verify enum values match what BleGattServer returns as JSON
    CHECK(static_cast<int>(FocusState::FOCUSED) == 0, "FOCUSED=0");
    CHECK(static_cast<int>(FocusState::LOOKING_AWAY) == 1, "LOOKING_AWAY=1");
    CHECK(static_cast<int>(FocusState::SLEEPY_OR_SLUMPED) == 2, "SLEEPY=2");
    CHECK(static_cast<int>(FocusState::PHONE_IN_USE) == 3, "PHONE_IN_USE=3");
    CHECK(static_cast<int>(FocusState::AWAY) == 4, "AWAY=4");
    CHECK(static_cast<int>(FocusState::LOW_CONFIDENCE) == 5, "LOW_CONFIDENCE=5");
    CHECK(static_cast<int>(FocusState::PAUSED) == 6, "PAUSED=6");
    CHECK(static_cast<int>(FocusState::CALIBRATING) == 7, "CALIBRATING=7");
    CHECK(static_cast<int>(FocusState::PHONE_IN_LEARNING) == 8, "PHONE_IN_LEARNING=8");
    CHECK(static_cast<int>(FocusState::PHONE_IN_PLAYING) == 9, "PHONE_IN_PLAYING=9");

    // Verify SystemMode enum
    CHECK(static_cast<int>(SystemMode::NORMAL) == 0, "NORMAL=0");
    CHECK(static_cast<int>(SystemMode::SILENT) == 1, "SILENT=1");
    CHECK(static_cast<int>(SystemMode::HOME) == 2, "HOME=2");
}

int main() {
    std::cout << "=== Module Integration Tests ===\n";

    test_behavior_to_policy_contract();
    test_policy_to_device_contract();
    test_session_info_contract();
    test_snapshot_to_gatt_contract();
    test_mode_sync_contract();
    test_report_contract();
    test_session_lifecycle();
    test_focus_state_enum_consistency();

    std::cout << "\n=== Results ===\n";
    std::cout << "Passed: " << tests_passed << std::endl;
    std::cout << "Failed: " << tests_failed << std::endl;

    if (tests_failed > 0) {
        std::cout << "SOME TESTS FAILED!\n";
        return 1;
    }
    std::cout << "ALL INTEGRATION TESTS PASSED!\n";
    return 0;
}
