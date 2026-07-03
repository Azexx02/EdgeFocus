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
#include "app/SessionController.h"
#include "app/AppContext.h"
#include "app/SessionPhase.h"
#include "storage/SQLiteStore.h"
#include "storage/ReportGenerator.h"
#include "behavior/FocusStateMachine.h"
#include "behavior/BehaviorEngine.h"
#include "policy/ReminderPolicy.h"
#include "policy/ModeManager.h"
#include "device/DeviceManager.h"
#include "network/BleGattServer.h"
#include "ui/StatusPage.h"
#include "ui/UIManager.h"
#include "vision/VisionResult.h"
#include <iostream>
#include <cassert>
#include <string>
#include <sstream>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

using namespace edgefocus;
using json = nlohmann::json;

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

void setBehaviorContext(SystemStateStore& store, int level, int risk = 70, bool phone_signal = true) {
    BehaviorContext ctx;
    ctx.phone_signal = phone_signal;
    ctx.vision_signal = false;
    if (phone_signal) {
        ctx.phone_distraction_level = level;
        ctx.phone_usage_state = 1;
    } else {
        ctx.phone_distraction_level = -1;
        ctx.phone_usage_state = -1;
    }
    ctx.risk_score = risk;
    ctx.reason = "integration";
    store.updateBehaviorContext(ctx);
}

void test_session_phase_helper() {
    std::cout << "\n[Test 0] Session phase helper\n";

    SessionInfo inactive;
    CHECK(deriveSessionPhase(inactive) == SessionPhase::IDLE, "inactive session => IDLE");

    SessionInfo precheck{};
    precheck.active = true;
    precheck.calibration_completed = false;
    CHECK(deriveSessionPhase(precheck) == SessionPhase::PRECHECK,
          "active + calibration incomplete => PRECHECK");

    SessionInfo focusing{};
    focusing.active = true;
    focusing.calibration_completed = true;
    CHECK(deriveSessionPhase(focusing) == SessionPhase::FOCUSING,
          "active + calibration complete => FOCUSING");

    SessionInfo paused{};
    paused.active = true;
    paused.paused = true;
    paused.calibration_completed = true;
    CHECK(deriveSessionPhase(paused) == SessionPhase::PAUSED, "paused session => PAUSED");
}

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

    setBehaviorContext(store, 2, 70);
    // PHONE_IN_USE is a behavior-layer state and maps via behavior context.
    store.updateFocusState(FocusState::PHONE_IN_USE, 5000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 12000);
    CHECK(cmd.has_value(), "PHONE_IN_USE → policy generates command");
    CHECK(cmd->level == CommandLevel::LEVEL_2, "PHONE_IN_USE can produce LEVEL_2");

    setBehaviorContext(store, 1, 20);
    // Low-risk PHONE_IN_PLAYING should stay lower than LEVEL_3.
    store.updateFocusState(FocusState::PHONE_IN_PLAYING, 17000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 22000);
    CHECK(cmd.has_value(), "PHONE_IN_PLAYING low-risk generates command");
    CHECK(cmd->level == CommandLevel::LEVEL_1, "PHONE_IN_PLAYING low-risk is LEVEL_1");

    setBehaviorContext(store, 3, 95);
    // High-risk PHONE_IN_USE can escalate to LEVEL_3.
    store.updateFocusState(FocusState::PHONE_IN_USE, 25000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 28000);
    CHECK(cmd.has_value(), "PHONE_IN_USE high-risk can escalate");
    CHECK(cmd->level == CommandLevel::LEVEL_3, "PHONE_IN_USE high-risk can be LEVEL_3");

    // PHONE_IN_LEARNING is still a phone input state; policy decides level
    // from risk context instead of hard-coding "no intervention".
    setBehaviorContext(store, 1, 20);
    store.updateFocusState(FocusState::PHONE_IN_LEARNING, 33000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 34000);
    CHECK(cmd.has_value(), "PHONE_IN_LEARNING → policy generates command");
    CHECK(cmd->level == CommandLevel::LEVEL_1, "PHONE_IN_LEARNING can be LEVEL_1");

    // SLEEPY_OR_SLUMPED → REST_SUGGESTION
    store.updateFocusState(FocusState::SLEEPY_OR_SLUMPED, 39000, "test");
    snap = store.snapshot();
    cmd = policy.processState(snap.focus_state, 40000);
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
    BleGattServer server(store, sqlite, policy, policy.getModeManager());
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
    }

    // get_console_data
    {
        std::string resp = server.handleCommand("{\"cmd\":\"get_console_data\"}");
        CHECK(resp.find("\"session\"") != std::string::npos, "has session");
        CHECK(resp.find("\"device\"") != std::string::npos, "has device");
        CHECK(resp.find("\"reminder\"") != std::string::npos, "has reminder");
        CHECK(resp.find("\"today\"") != std::string::npos, "has today");
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

    // Verify ReminderPolicy reads the updated mode.
    // SILENT changes the reminder medium marker, not the policy severity.
    setBehaviorContext(store, 2, 70);
    store.updateFocusState(FocusState::PHONE_IN_USE, 1000, "test");
    auto silent_cmd = policy.processState(FocusState::PHONE_IN_USE, 8000);
    CHECK(silent_cmd.has_value(), "PHONE_IN_USE generates command");
    CHECK(silent_cmd->message == "Phone usage detected", "SILENT keeps policy message unchanged");

    mm.setMode(SystemMode::NORMAL);
    auto normal_cmd = policy.processState(FocusState::PHONE_IN_USE, 16000);
    CHECK(normal_cmd.has_value(), "PHONE_IN_USE generates command");
    CHECK(normal_cmd->level == silent_cmd->level, "NORMAL and SILENT keep the same policy level");
    CHECK(normal_cmd->message == silent_cmd->message, "NORMAL and SILENT keep the same policy message");

    mm.setMode(SystemMode::HOME);
    auto home_first = policy.processState(FocusState::LOOKING_AWAY, 24000);
    CHECK(home_first.has_value(), "HOME mode first reminder still works");
    auto home_during_cooldown = policy.processState(FocusState::LOOKING_AWAY, 27000);
    CHECK(!home_during_cooldown.has_value(), "HOME mode does not shorten cooldown");

    // Also verify store is updated (for UI display)
    mm.setMode(SystemMode::NORMAL);
    DeviceStatus ds = store.snapshot().device;
    ds.system_mode = mm.getMode();
    store.updateDeviceStatus(ds);
    CHECK(store.snapshot().device.system_mode == SystemMode::NORMAL, "Store reflects mode");
}

void test_policy_skips_non_reminder_states_without_side_effects() {
    std::cout << "\n[Test 5.5] ReminderPolicy skips non-reminder states cleanly\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    SessionInfo session;
    session.active = true;
    session.calibration_completed = true;
    session.session_id = "policy_skip_states";
    store.updateSession(session);

    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite, dev_mgr);
    policy.init(ctx);

    BehaviorContext high_risk_phone;
    high_risk_phone.phone_signal = true;
    high_risk_phone.phone_distraction_level = 3;
    high_risk_phone.risk_score = 95;
    high_risk_phone.reason = "phone";
    store.updateBehaviorContext(high_risk_phone);

    auto low_conf = policy.processState(FocusState::LOW_CONFIDENCE, 1000);
    CHECK(!low_conf.has_value(), "LOW_CONFIDENCE produces no reminder command");
    auto after_low_conf = store.snapshot();
    CHECK(after_low_conf.session.reminder_count == 0, "LOW_CONFIDENCE does not increment reminder_count");
    CHECK(after_low_conf.reminder.level == CommandLevel::NONE, "LOW_CONFIDENCE does not update active reminder");

    auto phone_after_low_conf = policy.processState(FocusState::PHONE_IN_USE, 2000);
    CHECK(phone_after_low_conf.has_value(), "LOW_CONFIDENCE does not block next real phone reminder");
    CHECK(phone_after_low_conf->level == CommandLevel::LEVEL_3, "High-risk phone reminder still escalates");

    BehaviorContext neutral_ctx;
    neutral_ctx.reason = "away";
    store.updateBehaviorContext(neutral_ctx);
    auto away = policy.processState(FocusState::AWAY, 8000);
    CHECK(!away.has_value(), "AWAY produces no reminder command");
    auto after_away = store.snapshot();
    CHECK(after_away.session.reminder_count == 1, "AWAY does not add a fake reminder");
    CHECK(after_away.reminder.level == CommandLevel::NONE, "AWAY clears stale active reminder");

    auto looking_away = policy.processState(FocusState::LOOKING_AWAY, 9000);
    CHECK(looking_away.has_value(), "AWAY does not block next real visual reminder");
    CHECK(looking_away->level == CommandLevel::LEVEL_1, "Visual reminder level is preserved after AWAY");

    for (int i = 0; i < 6; ++i) {
        sqlite.runOnce(ctx);
    }
    auto events = sqlite.queryEvents("policy_skip_states");
    int reminder_events = 0;
    for (const auto& event : events) {
        if (event.type == StorageEventType::ReminderIssued) {
            reminder_events++;
        }
    }
    CHECK(reminder_events == 2, "Only real reminders are persisted");
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
    BleGattServer server(store, sqlite, policy, policy.getModeManager());
    server.init(ctx);

    // 1. Start session via BLE (new protocol: data nested, id echoed)
    std::string resp = server.handleCommand(
        R"({"cmd":"session_control","id":1,"data":{"action":"start","target_duration_min":25}})");
    CHECK(resp.find("\"status\":\"success\"") != std::string::npos, "Session started via BLE");

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
    resp = server.handleCommand(
        R"({"cmd":"session_control","id":2,"data":{"action":"end"}})");
    CHECK(resp.find("\"status\":\"success\"") != std::string::npos, "Session ended via BLE");
    auto end_json = json::parse(resp);
    CHECK(end_json["data"]["session_id"] == sid, "End response keeps session_id");
    CHECK(end_json["data"]["total_duration"].get<int64_t>() >= 0, "End response total_duration is non-negative");

    snap = store.snapshot();
    CHECK(snap.session.active == false, "Session is inactive after end");
    CHECK(snap.today.sessions == 1, "Manual BLE end increments today sessions");
    CHECK(snap.today.total_seconds >= 0, "Manual BLE end updates today total seconds");
    CHECK(snap.today.events == 2, "Manual BLE end carries distraction count into today events");

    // 4. Verify report was generated
    resp = server.handleCommand(R"({"cmd":"get_last_report","id":3})");
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

// ============================================================================
// Test 9: Timer completion creates authoritative report snapshot
// ============================================================================
void test_timer_completion_report_snapshot() {
    std::cout << "\n[Test 9] Timer completion report snapshot\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(2, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    std::string pushed_event;
    store.setPushCallback([&](const std::string& event_json) {
        pushed_event = event_json;
    });

    auto initial = store.snapshot();
    SessionInfo s;
    s.active = true;
    s.session_id = "timer_complete_session";
    s.start_ms = initial.snapshot_ms - 2000;
    s.elapsed_ms = 2000;
    s.target_duration_ms = 1000;
    s.remaining_ms = 0;
    s.distraction_count = 2;
    s.reminder_count = 1;
    s.phone_distraction_count = 1;
    s.calibration_completed = true;
    store.updateSession(s);

    behavior.runOnce(ctx);

    auto snap = store.snapshot();
    CHECK(snap.session.active == false, "Timer completion ends active session");
    CHECK(snap.last_report.session_id == "timer_complete_session", "Timer completion stores last report session_id");
    CHECK(snap.last_report.total_duration >= 1, "Timer completion stores total duration");
    CHECK(snap.last_report.distraction_count == 2, "Timer completion stores distraction count");
    CHECK(snap.last_report.reminder_count == 1, "Timer completion stores reminder count");
    CHECK(snap.last_report.phone_distraction_count == 1, "Timer completion stores phone distraction count");
    CHECK(snap.today.sessions == 1, "Timer completion increments today sessions");
    CHECK(snap.today.phone_distraction_count == 1, "Timer completion increments today phone count");
    CHECK(!pushed_event.empty(), "Timer completion pushes an event");
    auto pushed = json::parse(pushed_event);
    CHECK(pushed["cmd"] == "push_timer_complete", "Timer completion pushes push_timer_complete");
    CHECK(pushed["data"]["session_id"] == "timer_complete_session", "Timer push carries session_id");
    CHECK(pushed["data"].contains("start_time_ms"), "Timer push carries start_time_ms");
    CHECK(pushed["data"].contains("end_time_ms"), "Timer push carries end_time_ms");
    CHECK(pushed["data"].contains("total_duration"), "Timer push carries total_duration");
    CHECK(pushed["data"].contains("focus_duration"), "Timer push carries focus_duration");
    CHECK(pushed["data"]["target_duration_ms"] == 1000, "Timer push carries target_duration_ms");
    CHECK(pushed["data"]["end_reason"] == "timer_completed", "Timer push carries timer_completed reason");

    behavior.runOnce(ctx);
    snap = store.snapshot();
    CHECK(snap.today.sessions == 1, "Timer completion is not counted twice after idle cleanup");
    CHECK(snap.today.total_seconds == snap.last_report.total_duration, "Timer completion total seconds stays stable");
}

void test_away_timeout_uses_authoritative_session_end() {
    std::cout << "\n[Test 9.5] Away timeout uses authoritative session end\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(2, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    SessionController controller(store, sqlite);
    auto started = controller.startSession(25, "away_timeout_test");
    CHECK(started.success, "Away timeout test starts session through controller");

    std::string pushed_event;
    store.setPushCallback([&](const std::string& event_json) {
        pushed_event = event_json;
    });

    auto initial = store.snapshot();
    SessionInfo s = store.snapshot().session;
    s.start_ms = initial.snapshot_ms - 90000;
    s.elapsed_ms = 90000;
    s.target_duration_ms = 25 * 60000;
    s.remaining_ms = s.target_duration_ms - s.elapsed_ms;
    s.focus_accumulated_ms = 30000;
    s.distraction_count = 3;
    s.reminder_count = 2;
    s.phone_distraction_count = 1;
    s.away_since_ms = initial.snapshot_ms - 60000;
    s.away_timeout_ms = 1000;
    s.calibration_completed = true;
    store.updateSession(s);

    VisionResult result;
    result.timestamp_ms = initial.snapshot_ms;
    result.frame_quality = {FrameQualityLevel::GOOD, 0.9f, true, "good"};
    result.presence = {Presence::PRESENT, 0.9f, true};
    CHECK(vision_queue.push(result), "Away timeout test queues a vision result");

    behavior.runOnce(ctx);

    auto snap = store.snapshot();
    CHECK(snap.session.active == false, "Away timeout ends active session");
    CHECK(snap.last_report.session_id == started.session.session_id, "Away timeout stores last report session_id");
    CHECK(snap.last_report.total_duration == 90, "Away timeout stores total duration");
    CHECK(snap.last_report.focus_duration == 30, "Away timeout preserves accumulated focus duration");
    CHECK(snap.today.sessions == 1, "Away timeout increments today sessions");
    CHECK(snap.today.total_seconds == 90, "Away timeout increments today total seconds");
    CHECK(snap.today.events == 3, "Away timeout carries distraction count into today events");

    auto rec = sqlite.queryLastCompletedSession();
    CHECK(rec.session_id == started.session.session_id, "Away timeout persists completed session");
    CHECK(rec.total_duration == 90, "Away timeout persists total duration");

    CHECK(!pushed_event.empty(), "Away timeout pushes session end");
    auto pushed = json::parse(pushed_event);
    CHECK(pushed["cmd"] == "push_session_end", "Away timeout pushes push_session_end");
    CHECK(pushed["data"]["session_id"] == started.session.session_id, "Away timeout push carries session_id");
    CHECK(pushed["data"].contains("start_time_ms"), "Away timeout push carries start_time_ms");
    CHECK(pushed["data"].contains("end_time_ms"), "Away timeout push carries end_time_ms");
    CHECK(pushed["data"].contains("total_duration"), "Away timeout push carries total_duration");
    CHECK(pushed["data"].contains("focus_duration"), "Away timeout push carries focus_duration");
    CHECK(pushed["data"]["end_reason"] == "away_timeout", "Away timeout push carries end reason");
}

// ============================================================================
// Test 10: Vision frames do not auto-complete manual gimbal pre-check
// ============================================================================
void test_calibration_completion_push_fields() {
    std::cout << "\n[Test 10] Vision frames do not complete pre-check\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(4, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    SessionInfo s;
    s.active = true;
    s.session_id = "calibration_session";
    s.start_ms = store.snapshot().snapshot_ms;
    s.target_duration_ms = 25 * 60000;
    s.remaining_ms = s.target_duration_ms;
    s.calibrating = true;
    s.calibration_completed = false;
    store.updateSession(s);

    std::string pushed_event;
    store.setPushCallback([&](const std::string& event_json) {
        pushed_event = event_json;
    });

    VisionResult first;
    first.timestamp_ms = s.start_ms;
    first.frame_quality = {FrameQualityLevel::GOOD, 0.9f, true, "good"};
    first.presence = {Presence::PRESENT, 0.9f, true};
    CHECK(vision_queue.push(first), "First calibration frame queued");
    behavior.runOnce(ctx);

    VisionResult second = first;
    second.timestamp_ms = s.start_ms + 1500;
    CHECK(vision_queue.push(second), "Second calibration frame queued");
    behavior.runOnce(ctx);

    auto snap = store.snapshot();
    CHECK(snap.session.calibration_completed == false, "Vision frames do not auto-complete pre-check");
    CHECK(snap.session.calibrating == true, "Session remains in pre-check until manual skip");
    CHECK(pushed_event.empty(), "No calibration-complete push is emitted from vision frames");
}

// ============================================================================
// Test 11: Gimbal pre-check waits for manual skip without vision frames
// ============================================================================
void test_calibration_placeholder_without_frames() {
    std::cout << "\n[Test 11] Calibration waits without frames\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(4, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    auto initial = store.snapshot();
    SessionInfo s;
    s.active = true;
    s.session_id = "placeholder_session";
    s.start_ms = initial.snapshot_ms - 5000;
    s.target_duration_ms = 25 * 60000;
    s.remaining_ms = s.target_duration_ms;
    s.calibrating = true;
    s.calibration_completed = false;
    store.updateSession(s);

    std::string pushed_event;
    store.setPushCallback([&](const std::string& event_json) {
        pushed_event = event_json;
    });

    behavior.runOnce(ctx);

    auto snap = store.snapshot();
    CHECK(snap.session.calibration_completed == false, "Pre-check does not auto-complete without manual skip");
    CHECK(snap.session.calibrating == true, "Pre-check remains calibrating without manual skip");
    CHECK(snap.session.elapsed_ms == 0, "Pre-check does not advance session elapsed time");
    CHECK(snap.session.remaining_ms == s.target_duration_ms, "Pre-check does not reduce remaining time");
    CHECK(pushed_event.empty(), "No automatic calibration push without manual skip");
}

// ============================================================================
// Test 12: Local StatusPage lifecycle supports manual calibr. skip, pause/resume, end
// ============================================================================
void test_status_page_local_lifecycle_persists_session() {
    std::cout << "\n[Test 12] StatusPage local lifecycle persists session\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);
    ReportGenerator report_gen(sqlite);

    StatusPage page;
    page.setStore(&store);
    page.setSQLiteStore(&sqlite);
    page.setReportGenerator(&report_gen);

    bool switched_to_review = false;
    page.setPageSwitcher([&](int page_id) {
        switched_to_review = (page_id == 3);
    });

    std::string last_push;
    store.setPushCallback([&](const std::string& event_json) {
        last_push = event_json;
    });

    CHECK(page.onTouch(120, 190), "Local START touch is consumed");
    auto snap = store.snapshot();
    CHECK(snap.session.active == true, "Local START activates session");
    CHECK(!snap.session.session_id.empty(), "Local START creates session_id");
    CHECK(snap.session.calibration_completed == false, "Local START keeps calibration incomplete");
    CHECK(snap.session.calibrating == true, "Local START starts calibration");
    CHECK(last_push.empty(), "Local START does not push precheck state to App");
    std::string sid = snap.session.session_id;

    last_push.clear();
    CHECK(page.onTouch(120, 270), "Local GO touch is consumed");
    snap = store.snapshot();
    CHECK(snap.session.calibration_completed == true, "Local GO completes calibration");
    CHECK(snap.session.calibrating == false, "Local GO disables calibration flag");
    CHECK(snap.session.elapsed_ms == 0, "Local GO starts focus timer at zero");
    CHECK(snap.session.remaining_ms == snap.session.target_duration_ms, "Local GO starts with full remaining time");
    CHECK(snap.focus_state == FocusState::FOCUSED, "Manual GO sets focus state");
    CHECK(!last_push.empty(), "Local GO pushes state to subscribed App");
    auto go_push = json::parse(last_push);
    CHECK(go_push["cmd"] == "push_state", "Local GO push uses existing push_state event");
    CHECK(go_push["data"]["session_phase"] == "focusing", "Local GO push carries focusing phase");
    CHECK(go_push["data"]["calibration_completed"] == true,
          "Local GO push carries completed precheck state");

    SessionInfo s = snap.session;
    s.start_ms = store.snapshot().snapshot_ms - 60000;
    s.elapsed_ms = 60000;
    s.distraction_count = 2;
    s.reminder_count = 1;
    s.phone_distraction_count = 1;
    store.updateSession(s);

    last_push.clear();
    CHECK(page.onTouch(120, 270), "Local PAUSE touch is consumed");
    snap = store.snapshot();
    CHECK(snap.session.active == true, "Local PAUSE keeps session active");
    CHECK(snap.session.paused == true, "Local PAUSE marks session paused");
    CHECK(snap.session.session_id == sid, "Local PAUSE preserves session_id");
    CHECK(page.currentInternalStateForTest() == StatusPage::PAUSED_STATE, "Local PAUSE shows paused page");
    CHECK(!switched_to_review, "Local PAUSE does not switch to review");
    CHECK(!last_push.empty(), "Local PAUSE pushes state to subscribed App");
    auto pause_push = json::parse(last_push);
    CHECK(pause_push["cmd"] == "push_state", "Local PAUSE push uses existing push_state event");
    CHECK(pause_push["data"]["paused"] == true, "Local PAUSE push carries paused=true");
    CHECK(pause_push["data"]["session_phase"] == "paused", "Local PAUSE push carries paused phase");

    last_push.clear();
    CHECK(page.onTouch(120, 210), "Local RESUME touch is consumed");
    snap = store.snapshot();
    CHECK(snap.session.active == true, "Local RESUME keeps session active");
    CHECK(snap.session.paused == false, "Local RESUME unpauses session");
    CHECK(snap.session.session_id == sid, "Local RESUME keeps session_id");
    CHECK(page.currentInternalStateForTest() == StatusPage::FOCUSED_STATE, "Local RESUME returns to focused state");
    CHECK(!last_push.empty(), "Local RESUME pushes state to subscribed App");
    auto resume_push = json::parse(last_push);
    CHECK(resume_push["cmd"] == "push_state", "Local RESUME push uses existing push_state event");
    CHECK(resume_push["data"]["paused"] == false, "Local RESUME push carries paused=false");
    CHECK(resume_push["data"]["session_phase"] == "focusing", "Local RESUME push carries focusing phase");

    CHECK(page.onTouch(120, 270), "Local re-PAUSE touch is consumed");
    snap = store.snapshot();
    CHECK(snap.session.paused == true, "Local re-PAUSE re-enters paused state");

    last_push.clear();
    CHECK(page.onTouch(120, 270), "Local END touch is consumed");
    snap = store.snapshot();
    CHECK(switched_to_review, "Local END switches to review");
    CHECK(!last_push.empty(), "Local END pushes session end to subscribed App");
    auto end_push = json::parse(last_push);
    CHECK(end_push["cmd"] == "push_session_end", "Local END push uses existing push_session_end event");
    CHECK(end_push["data"]["session_id"] == sid, "Local END push carries authoritative session_id");
    CHECK(snap.last_report.session_id == sid, "Local END stores last report session_id");
    CHECK(snap.last_report.total_duration == 60, "Local END stores total duration");
    CHECK(snap.last_report.distraction_count == 2, "Local END stores distraction count");
    CHECK(snap.today.sessions == 1, "Local END updates today sessions");
    CHECK(snap.today.phone_distraction_count == 1, "Local END updates today phone count");

    auto rec = sqlite.queryLastCompletedSession();
    CHECK(rec.session_id == sid, "Local END persists completed session");
    CHECK(rec.total_duration == 60, "Local END persists total duration");
}

// ============================================================================
// Test 12.5: StatusPage syncs UI state from snapshot
// ============================================================================
void test_status_page_syncs_with_session_snapshot() {
    std::cout << "\n[Test 12.5] StatusPage syncs session snapshot\n";

    SystemStateStore store;
    FrameBuffer fb;
    CHECK(fb.init(), "StatusPage sync test framebuffer init (headless fallback expected)");

    StatusPage page;
    page.setStore(&store);

    Preferences prefs;
    prefs.default_focus_duration_min = 45;
    store.updatePreferences(prefs);

    auto snap = store.snapshot();
    page.syncWithSnapshotForTest(snap);
    CHECK(page.currentInternalStateForTest() == StatusPage::READY_STATE, "StatusPage starts READY when session inactive");
    CHECK(page.targetDurationMinForTest() == 45, "StatusPage uses preference default duration when session inactive");

    CHECK(page.onTouch(190, 130), "StatusPage accepts local duration plus while READY");
    CHECK(page.targetDurationMinForTest() == 50, "StatusPage local duration override increments from preference default");
    page.render(fb, store.snapshot());
    CHECK(page.targetDurationMinForTest() == 50, "StatusPage keeps local duration override across idle render");

    SessionInfo running;
    running.active = true;
    running.calibration_completed = false;
    running.target_duration_ms = 30 * 60000;
    store.updateSession(running);
    snap = store.snapshot();
    page.render(fb, snap);
    CHECK(page.currentInternalStateForTest() == StatusPage::CALIBRATING_STATE,
          "StatusPage syncs ACTIVE + !calibration_completed to CALIBRATING");
    CHECK(page.targetDurationMinForTest() == 30, "StatusPage syncs active session target duration to UI minutes");

    running.calibration_completed = true;
    running.elapsed_ms = 5000;
    running.remaining_ms = 55000;
    store.updateSession(running);
    page.syncWithSnapshotForTest(store.snapshot());
    CHECK(page.currentInternalStateForTest() == StatusPage::FOCUSED_STATE,
          "StatusPage syncs ACTIVE + calibration_completed to FOCUSED");
    CHECK(page.countdownSecondsForTest(store.snapshot()) == 55,
          "StatusPage countdown uses authoritative remaining_ms");

    running.paused = true;
    store.updateSession(running);
    page.syncWithSnapshotForTest(store.snapshot());
    CHECK(page.currentInternalStateForTest() == StatusPage::PAUSED_STATE,
          "StatusPage syncs ACTIVE + paused to PAUSED");
    running.paused = false;

    running.active = false;
    store.updateSession(running);
    page.syncWithSnapshotForTest(store.snapshot());
    CHECK(page.currentInternalStateForTest() == StatusPage::READY_STATE,
          "StatusPage syncs inactive session to READY");
    CHECK(page.targetDurationMinForTest() == 45,
          "StatusPage resets to preference default after session becomes inactive");
}

void test_status_page_precheck_view_model_uses_camera_guidance() {
    std::cout << "\n[Test 12.55] StatusPage precheck view model uses camera guidance\n";

    SystemSnapshot snap;
    snap.session.active = true;
    snap.session.calibration_completed = false;
    snap.device.camera_ok = true;
    snap.camera_preview_image.available = true;
    snap.camera_preview_image.sequence = 7;
    snap.frame_quality.score = 0.72f;
    snap.frame_quality.distance_hint = "move_closer";
    snap.frame_quality.pose_hint = "center_face";
    snap.frame_quality.precheck_ready = false;
    snap.preferences.gimbal_enabled = true;
    snap.device.gimbal_connected = true;
    snap.device.gimbal_online = false;
    snap.device.gimbal_safe = false;

    auto model = StatusPage::precheckViewModelForTest(snap, 25);
    CHECK(model.camera_status == "CAM OK", "Precheck view shows camera OK");
    CHECK(model.preview_status == "PREVIEW READY", "Precheck view shows preview availability");
    CHECK(model.quality_status == "72%", "Precheck view shows frame quality percent");
    CHECK(model.distance_status == "Dist: Move closer", "Precheck view shows distance guidance");
    CHECK(model.pose_status == "Pose: Center face", "Precheck view shows pose guidance");
    CHECK(model.primary_tip == "Adjust position", "Precheck view asks for adjustment before ready");
    CHECK(model.action_label == "START FOCUS", "Precheck view uses continue action label");
    CHECK(model.action_enabled == true, "Board-owned precheck keeps board continue action enabled");
    CHECK(model.show_gimbal == false, "Precheck view hides unsafe gimbal from normal flow");

    snap.session.precheck_owner = "app";
    model = StatusPage::precheckViewModelForTest(snap, 25);
    CHECK(model.action_enabled == false, "App-owned precheck disables board continue action");
    CHECK(model.action_label == "USE APP", "App-owned precheck tells user to continue on App");

    snap.session.precheck_owner = "board";
    snap.frame_quality.precheck_ready = true;
    snap.device.gimbal_online = true;
    snap.device.gimbal_safe = true;
    model = StatusPage::precheckViewModelForTest(snap, 25);
    CHECK(model.primary_tip == "Ready to focus", "Precheck view shows ready state");
    CHECK(model.show_gimbal == true, "Precheck view shows gimbal only when ready");
    CHECK(model.gimbal_status == "GIMBAL READY", "Precheck view marks ready gimbal");
}

void test_session_precheck_owner_stays_internal_to_start_surface() {
    std::cout << "\n[Test 12.56] Session precheck owner follows start surface internally\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    SessionController controller(store, sqlite);
    auto app_start = controller.startSession(25, "app");
    CHECK(app_start.success, "App session start succeeds");
    CHECK(app_start.session.precheck_owner == "app", "App start owns App precheck");
    CHECK(store.snapshot().session.precheck_owner == "app", "App precheck owner is stored internally");

    CHECK(controller.endSession("test_end").success, "App-owned precheck session can end");

    auto local_start = controller.startSession(25, "local");
    CHECK(local_start.success, "Local board session start succeeds");
    CHECK(local_start.session.precheck_owner == "board", "Board start owns board precheck");
    CHECK(store.snapshot().session.precheck_owner == "board", "Board precheck owner is stored internally");
}

void test_status_page_does_not_skip_app_owned_precheck() {
    std::cout << "\n[Test 12.57] StatusPage does not skip App-owned precheck\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    StatusPage page;
    page.setStore(&store);
    page.setSQLiteStore(&sqlite);

    SessionController controller(store, sqlite);
    auto started = controller.startSession(25, "app");
    CHECK(started.success, "App session start succeeds for owner test");
    page.syncWithSnapshotForTest(store.snapshot());

    CHECK(page.onTouch(120, 290) == false, "Board touch cannot continue App-owned precheck");
    CHECK(store.snapshot().session.calibration_completed == false,
          "App-owned precheck remains incomplete after board touch");
    CHECK(store.snapshot().focus_state == FocusState::CALIBRATING,
          "App-owned precheck remains in CALIBRATING after board touch");
}

void test_status_page_alert_copy_mapping() {
    std::cout << "\n[Test 12.6] StatusPage maps reminder copy by reminder semantics\n";

    auto rest_copy = StatusPage::alertHeadlineForTest(CommandLevel::REST_SUGGESTION, "Take a short break");
    CHECK(rest_copy.first == "REST", "REST_SUGGESTION primary title is REST");
    CHECK(rest_copy.second == "NOW", "REST_SUGGESTION secondary title is NOW");

    auto phone_copy = StatusPage::alertHeadlineForTest(CommandLevel::LEVEL_2, "Phone usage detected");
    CHECK(phone_copy.first == "BACK TO", "Phone reminder primary title avoids REST copy");
    CHECK(phone_copy.second == "FOCUS", "Phone reminder secondary title avoids REST copy");

    auto high_copy = StatusPage::alertHeadlineForTest(CommandLevel::LEVEL_3, "High phone distraction detected");
    CHECK(high_copy.first == "STAY", "High-severity focus reminder uses focus copy");
    CHECK(high_copy.second == "FOCUSED", "High-severity focus reminder uses focus copy");
}

// ============================================================================
// Test 13: UIManager auto-switches to status when session becomes active
// ============================================================================
void test_uimanager_auto_switch_to_status_on_session_start() {
    std::cout << "\n[Test 13] UIManager auto-switches on session activation\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    UIManager ui(store);
    CHECK(ui.init(ctx), "UIManager init succeeds");

    ui.setCurrentPageForTest(1); // PAGE_STATS
    CHECK(ui.currentPageForTest() == 1, "UIManager test can set current page to a non-status page");

    SessionInfo running = store.snapshot().session;
    running.active = true;
    running.calibration_completed = true;
    running.target_duration_ms = 25 * 60000;
    store.updateSession(running);

    ui.runOnce(ctx);
    CHECK(ui.currentPageForTest() == 0, "UIManager switches to Status page when session becomes active");

    ui.shutdown(ctx);
}

void test_uimanager_phone_screen_route_bypasses_board_screen_hardware() {
    std::cout << "\n[Test 13.1] UIManager bypasses board screen when phone owns display\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    Preferences prefs = store.snapshot().preferences;
    prefs.screen_output_route = "phone";
    store.updatePreferences(prefs);

    UIManager ui(store);
    CHECK(ui.init(ctx), "UIManager init succeeds with phone screen route");
    CHECK(ui.screenHardwareBypassedForTest(),
          "Phone screen route bypasses board screen and touch hardware");
    CHECK(!ui.framebufferIsRealForTest(),
          "Phone screen route uses offscreen framebuffer only");

    ui.shutdown(ctx);
}

// ============================================================================
// Test 13: SessionController owns authoritative lifecycle primitives
// ============================================================================
void test_session_controller_lifecycle_and_recovery() {
    std::cout << "\n[Test 13] SessionController lifecycle and recovery\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    SessionController controller(store, sqlite);
    auto start = controller.startSession(25, "test");
    CHECK(start.success, "SessionController start succeeds");
    CHECK(start.session.active, "SessionController start returns active session");
    CHECK(start.session.target_duration_ms == 25 * 60000, "SessionController start stores target duration");
    CHECK(start.session.calibration_completed == false, "SessionController start leaves calibration incomplete");
    CHECK(controller.skipPrecheck("integration_skip"), "SessionController skip precheck succeeds");

    SessionInfo pre_pause = store.snapshot().session;
    pre_pause.start_ms = store.snapshot().snapshot_ms - 5000;
    pre_pause.elapsed_ms = 0;
    pre_pause.target_duration_ms = 25 * 60000;
    pre_pause.remaining_ms = pre_pause.target_duration_ms;
    pre_pause.calibration_completed = true;
    pre_pause.calibrating = false;
    store.updateSession(pre_pause);

    CHECK(controller.pauseSession("integration_pause"), "SessionController pause succeeds");
    auto paused_snap = store.snapshot();
    CHECK(paused_snap.session.active == true, "SessionController pause keeps session active");
    CHECK(paused_snap.session.paused == true, "SessionController pause marks session paused");
    CHECK(paused_snap.session.elapsed_ms >= 4000, "SessionController pause freezes elapsed at pause time");
    CHECK(paused_snap.session.remaining_ms <= paused_snap.session.target_duration_ms - 4000,
          "SessionController pause freezes remaining at pause time");
    CHECK(controller.resumeSession("integration_resume"), "SessionController resume succeeds");
    auto resumed_snap = store.snapshot();
    CHECK(resumed_snap.session.active == true, "SessionController resume keeps session active");
    CHECK(resumed_snap.session.paused == false, "SessionController resume clears pause");
    CHECK(resumed_snap.session.session_id == start.session.session_id, "SessionController resume preserves session_id");
    CHECK(deriveSessionPhase(resumed_snap.session) == SessionPhase::FOCUSING,
          "SessionController resume returns to focusing after precheck was skipped");

    SessionInfo s = store.snapshot().session;
    s.elapsed_ms = 90000;
    s.distraction_count = 3;
    s.reminder_count = 2;
    s.phone_distraction_count = 1;
    store.updateSession(s);

    auto end = controller.endSession("test_ended");
    CHECK(end.success, "SessionController end succeeds");
    CHECK(end.report.session_id == start.session.session_id, "SessionController end keeps session id");
    CHECK(end.report.total_duration == 90, "SessionController end freezes total duration");
    CHECK(store.snapshot().session.active == false, "SessionController end resets active session");
    CHECK(store.snapshot().today.sessions == 1, "SessionController end updates today sessions");
    CHECK(store.snapshot().today.total_seconds == 90, "SessionController end updates today total seconds");
    CHECK(store.snapshot().today.events == 3, "SessionController end updates today events");

    auto rec = sqlite.queryLastCompletedSession();
    CHECK(rec.session_id == start.session.session_id, "SessionController end persists completed session");
    CHECK(rec.total_duration == 90, "SessionController end persists total duration");

    controller.startSession(10, "test");
    int recovered = controller.closeInterruptedSessions();
    CHECK(recovered >= 1, "SessionController recovery closes active persisted sessions");
    auto interrupted = sqlite.queryLastCompletedSession();
    CHECK(interrupted.session_id.empty() == false, "SessionController recovery leaves completed history");
}

void test_session_controller_end_focus_accumulation() {
    std::cout << "\n[Test 13.5] SessionController manual end uses focus_accumulated_ms\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    SessionController controller(store, sqlite);
    auto start = controller.startSession(20, "test_focus_accum");
    CHECK(start.success, "SessionController start succeeds for focus accumulation scenario");
    CHECK(start.session.focus_accumulated_ms == 0, "Started session focus_accumulated_ms initializes to 0");

    SessionInfo s = store.snapshot().session;
    s.active = true;
    s.calibration_completed = true;
    s.elapsed_ms = 90000;
    s.focus_accumulated_ms = 30000;
    s.distraction_count = 0;
    s.reminder_count = 0;
    s.phone_distraction_count = 0;
    s.start_ms = std::max<int64_t>(0, store.snapshot().snapshot_ms - 90000);
    store.updateSession(s);
    store.updateFocusState(FocusState::FOCUSED, s.start_ms, "focused");

    auto end = controller.endSession("manual_end_focus_accum");
    CHECK(end.success, "SessionController end succeeds");
    CHECK(end.report.focus_duration == 30, "Manual end uses focus_accumulated (30 seconds)");
    CHECK(end.report.total_duration == 90, "Manual end still reports elapsed total_duration 90 seconds");
    CHECK(end.report.session_id == start.session.session_id, "Manual end preserves session_id");
}

void test_session_controller_end_refreshes_stale_elapsed() {
    std::cout << "\n[Test 13.6] SessionController end refreshes stale elapsed_ms\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    SessionController controller(store, sqlite);
    auto start = controller.startSession(20, "test_end_refresh");
    CHECK(start.success, "SessionController start succeeds");
    CHECK(start.session.session_id.empty() == false, "Started session has session_id");

    auto base = store.snapshot();
    SessionInfo s = base.session;
    s.active = true;
    s.calibration_completed = true;
    s.elapsed_ms = 0;
    s.start_ms = std::max<int64_t>(0, base.snapshot_ms - 5000);
    s.session_id = base.session.session_id;
    s.target_duration_ms = base.session.target_duration_ms;
    s.remaining_ms = s.target_duration_ms;
    store.updateSession(s);

    auto ended = controller.endSession("integration_end_refresh");
    CHECK(ended.success, "SessionController end succeeds");
    CHECK(ended.report.total_duration >= 4, "Stale elapsed is refreshed to avoid zero-length report");
    CHECK(ended.report.session_id == s.session_id, "Ended report keeps session_id");
    CHECK(store.snapshot().session.active == false, "Session ends inactive");
}

void test_session_controller_end_is_idempotent() {
    std::cout << "\n[Test 13.65] SessionController end is idempotent\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    SessionController controller(store, sqlite);
    auto start = controller.startSession(20, "test_end_idempotent");
    CHECK(start.success, "Idempotent end test starts session");
    CHECK(start.session.session_id.empty() == false, "Idempotent end test has session_id");

    SessionInfo s = store.snapshot().session;
    s.active = true;
    s.calibration_completed = true;
    s.calibrating = false;
    s.elapsed_ms = 90000;
    s.focus_accumulated_ms = 30000;
    s.distraction_count = 3;
    s.reminder_count = 2;
    s.phone_distraction_count = 1;
    store.updateSession(s);

    auto first_end = controller.endSession("first_end");
    for (int i = 0; i < 4; ++i) sqlite.runOnce(ctx);
    auto after_first = store.snapshot();

    auto second_end = controller.endSession("second_end");
    for (int i = 0; i < 4; ++i) sqlite.runOnce(ctx);
    auto after_second = store.snapshot();

    CHECK(first_end.success, "First end succeeds");
    CHECK(second_end.success == false, "Second end is rejected once session already ended");
    CHECK(after_first.today.sessions == 1, "First end increments today sessions once");
    CHECK(after_second.today.sessions == 1, "Second end does not increment today sessions again");
    CHECK(after_first.today.events == 3, "First end stores today events once");
    CHECK(after_second.today.events == 3, "Second end does not duplicate today events");
    CHECK(after_second.today.total_seconds == 90, "Second end does not duplicate today total seconds");
    CHECK(after_second.last_report.session_id == start.session.session_id,
          "Second end preserves last_report session_id");
    CHECK(after_second.last_report.total_duration == 90,
          "Second end preserves last_report total_duration");
    CHECK(after_second.last_report.focus_duration == 30,
          "Second end preserves last_report focus_duration");

    auto rec = sqlite.queryLastCompletedSession();
    CHECK(rec.session_id == start.session.session_id, "SQLite completed session remains the original session");
    CHECK(rec.total_duration == 90, "SQLite completed session keeps original duration");
    CHECK(rec.focus_duration == 30, "SQLite completed session keeps original focus duration");

    auto events = sqlite.queryEvents(start.session.session_id);
    int session_end_events = 0;
    for (const auto& ev : events) {
        if (ev.type == StorageEventType::SessionEnd) {
            session_end_events++;
        }
    }
    CHECK(session_end_events == 1, "Only one SessionEnd event is persisted");
}

void test_session_controller_end_is_idempotent_under_concurrency() {
    std::cout << "\n[Test 13.66] SessionController end is idempotent under concurrency\n";

    for (int iteration = 0; iteration < 20; ++iteration) {
        SystemStateStore store;
        SQLiteStore sqlite(":memory:");
        AppContext ctx;
        sqlite.init(ctx);

        SessionController controller(store, sqlite);
        auto start = controller.startSession(20, "test_end_concurrent");
        CHECK(start.success, "Concurrent end test starts session");

        SessionInfo s = store.snapshot().session;
        s.active = true;
        s.calibration_completed = true;
        s.calibrating = false;
        s.elapsed_ms = 90000;
        s.focus_accumulated_ms = 30000;
        s.distraction_count = 3;
        s.reminder_count = 2;
        s.phone_distraction_count = 1;
        store.updateSession(s);

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        SessionController::EndResult first;
        SessionController::EndResult second;

        auto worker = [&](SessionController::EndResult* out, const char* reason) {
            ready.fetch_add(1);
            while (!go.load()) {
                std::this_thread::yield();
            }
            *out = controller.endSession(reason);
        };

        std::thread t1(worker, &first, "concurrent_end_1");
        std::thread t2(worker, &second, "concurrent_end_2");

        while (ready.load() < 2) {
            std::this_thread::yield();
        }
        go.store(true);

        t1.join();
        t2.join();

        for (int i = 0; i < 4; ++i) sqlite.runOnce(ctx);

        const int success_count = (first.success ? 1 : 0) + (second.success ? 1 : 0);
        auto snap = store.snapshot();
        auto rec = sqlite.queryLastCompletedSession();
        auto events = sqlite.queryEvents(start.session.session_id);
        int session_end_events = 0;
        for (const auto& ev : events) {
            if (ev.type == StorageEventType::SessionEnd) {
                session_end_events++;
            }
        }

        CHECK(success_count == 1, "Exactly one concurrent end succeeds");
        CHECK(snap.today.sessions == 1, "Concurrent end increments today sessions once");
        CHECK(snap.today.events == 3, "Concurrent end increments today events once");
        CHECK(snap.last_report.session_id == start.session.session_id,
              "Concurrent end keeps last_report session_id");
        CHECK(rec.session_id == start.session.session_id,
              "Concurrent end persists original session");
        CHECK(session_end_events == 1, "Concurrent end persists one SessionEnd event");
    }
}

void test_stale_session_update_cannot_resurrect_ended_session() {
    std::cout << "\n[Test 13.67] Stale session update cannot resurrect ended session\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    SessionController controller(store, sqlite);
    auto start = controller.startSession(20, "test_stale_update");
    CHECK(start.success, "Stale update test starts session");

    SessionInfo stale = store.snapshot().session;
    stale.calibration_completed = true;
    stale.calibrating = false;
    stale.elapsed_ms = 120000;
    stale.remaining_ms = 0;

    auto ended = controller.endSession("ended_before_stale_write");
    CHECK(ended.success, "Session ends before stale write");
    CHECK(store.snapshot().session.active == false, "Session inactive after end");

    CHECK(!store.updateSessionIfCurrent(stale), "Stale active session update is rejected");
    CHECK(store.snapshot().session.active == false, "Rejected stale update does not resurrect session");
    CHECK(store.snapshot().last_report.session_id == start.session.session_id,
          "Rejected stale update preserves last report");
}

void test_session_controller_precheck_does_not_count_as_focus_time() {
    std::cout << "\n[Test 13.7] SessionController precheck does not count as focus time\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    SessionController controller(store, sqlite);
    auto start = controller.startSession(1, "test_precheck_time");
    CHECK(start.success, "Precheck timing test starts session");

    auto snap = store.snapshot();
    SessionInfo s = snap.session;
    s.start_ms = std::max<int64_t>(0, snap.snapshot_ms - 55000);
    s.elapsed_ms = 0;
    s.remaining_ms = s.target_duration_ms;
    s.calibration_completed = false;
    s.calibrating = true;
    store.updateSession(s);

    CHECK(controller.pauseSession("precheck_pause"), "Precheck pause succeeds");
    auto paused = store.snapshot();
    CHECK(paused.session.paused == true, "Precheck pause marks paused");
    CHECK(paused.session.elapsed_ms == 0, "Precheck pause keeps elapsed at zero");
    CHECK(paused.session.remaining_ms == paused.session.target_duration_ms,
          "Precheck pause keeps full remaining time");

    CHECK(controller.resumeSession("precheck_resume"), "Precheck resume succeeds");
    auto resumed = store.snapshot();
    CHECK(resumed.session.paused == false, "Precheck resume clears paused");
    CHECK(resumed.session.calibration_completed == false, "Precheck resume stays before calibration skip");
    CHECK(deriveSessionPhase(resumed.session) == SessionPhase::PRECHECK,
          "Precheck resume returns to PRECHECK phase");
    CHECK(resumed.focus_state == FocusState::CALIBRATING, "Precheck resume restores CALIBRATING focus state");

    SessionInfo old_precheck = store.snapshot().session;
    old_precheck.start_ms = std::max<int64_t>(0, store.snapshot().snapshot_ms - 56000);
    old_precheck.elapsed_ms = 0;
    old_precheck.remaining_ms = old_precheck.target_duration_ms;
    store.updateSession(old_precheck);

    auto ended = controller.endSession("precheck_end");
    CHECK(ended.success, "Precheck end succeeds");
    CHECK(ended.report.total_duration == 0, "Precheck end reports zero focus session duration");
    CHECK(ended.report.focus_duration == 0, "Precheck end reports zero focus duration");
    CHECK(store.snapshot().today.total_seconds == 0, "Precheck end does not add waiting time to today total");
}

// ============================================================================
// Test 14: phone_sync drives store focus state without waiting for a vision frame
// ============================================================================
void test_phone_signal_updates_focus_state_immediately() {
    std::cout << "\n[Test 14] phone_sync updates focus state immediately\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(2, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    auto initial = store.snapshot();
    SessionInfo s;
    s.active = true;
    s.session_id = "phone_signal_session";
    s.start_ms = initial.snapshot_ms;
    s.target_duration_ms = 25 * 60000;
    s.remaining_ms = s.target_duration_ms;
    s.calibrating = false;
    s.calibration_completed = true;
    store.updateSession(s);
    store.updateFocusState(FocusState::FOCUSED, initial.snapshot_ms, "test focused");

    PhoneUsageSignal sig;
    sig.active = true;
    sig.distraction_level = 3;
    sig.timestamp_ms = initial.snapshot_ms + 1000;
    store.setPhoneSignal(sig);

    behavior.runOnce(ctx);

    auto snap = store.snapshot();
    CHECK(snap.focus_state == FocusState::PHONE_IN_USE, "Level 3 phone signal keeps generic phone usage state");
    CHECK(snap.session.phone_distraction_count == 1, "Active phone signal increments session phone count");
    CHECK(snap.session.current_phone_distraction_level == 3, "Active phone signal stores phone-reported strength for behavior tuning");
}

void test_inactive_phone_sync_clears_stale_pressure() {
    std::cout << "\n[Test 14.1] phone_sync inactive clears stale phone pressure\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(4, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite, dev_mgr);
    policy.init(ctx);

    auto initial = store.snapshot();
    SessionInfo s;
    s.active = true;
    s.session_id = "inactive_phone_sync_session";
    s.start_ms = initial.snapshot_ms;
    s.target_duration_ms = 25 * 60000;
    s.remaining_ms = s.target_duration_ms;
    s.calibrating = false;
    s.calibration_completed = true;
    store.updateSession(s);
    store.updateFocusState(FocusState::FOCUSED, initial.snapshot_ms, "test focused");

    PhoneUsageSignal active;
    active.active = true;
    active.distraction_level = 2;
    active.timestamp_ms = initial.snapshot_ms + 1000;
    active.usage_state = PhoneUsageState::PLAYING;
    store.setPhoneSignal(active);
    behavior.runOnce(ctx);

    auto running = store.snapshot();
    CHECK(running.focus_state == FocusState::PHONE_IN_PLAYING, "Active phone_sync enters phone state");
    CHECK(running.behavior_context.phone_signal == true, "Active phone_sync sets phone context");
    CHECK(running.behavior_context.phone_distraction_level == 2, "Active phone_sync stores distraction level");

    PhoneUsageSignal inactive;
    inactive.active = false;
    inactive.distraction_level = 2;
    inactive.timestamp_ms = initial.snapshot_ms + 2000;
    store.setPhoneSignal(inactive);
    behavior.runOnce(ctx);

    auto after = store.snapshot();
    CHECK(after.focus_state == FocusState::FOCUSED, "Inactive phone_sync drives FOCUSED reset");
    CHECK(after.focus_reason == "phone_sync_inactive", "Inactive phone_sync records explicit reason");
    CHECK(after.behavior_context.phone_signal == false, "Inactive phone_sync clears phone signal");
    CHECK(after.behavior_context.phone_distraction_level == -1,
          "Inactive phone_sync clears phone distraction level");
    CHECK(after.behavior_context.phone_usage_state == -1, "Inactive phone_sync clears phone usage state");
    CHECK(after.session.current_phone_distraction_level == 0, "Inactive phone_sync clears current phone level");

    auto cmd = policy.processState(after.focus_state, after.snapshot_ms + 10);
    CHECK(!cmd.has_value(), "FOCUSED state after inactive phone_sync emits no reminder");
}

void test_distraction_count_is_episode_based() {
    std::cout << "\n[Test 14.0.5] Distraction count increments on episode entry\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(8, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    auto initial = store.snapshot();
    SessionInfo s;
    s.active = true;
    s.session_id = "distraction_episode_session";
    s.start_ms = initial.snapshot_ms - 1000;
    s.target_duration_ms = 25 * 60000;
    s.remaining_ms = s.target_duration_ms;
    s.calibrating = false;
    s.calibration_completed = true;
    store.updateSession(s);
    store.updateFocusState(FocusState::FOCUSED, initial.snapshot_ms, "test focused");

    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = initial.snapshot_ms + 1;
    sig.distraction_level = 2;
    sig.usage_state = PhoneUsageState::IN_USE;
    store.setPhoneSignal(sig);
    behavior.runOnce(ctx);
    auto snap = store.snapshot();
    CHECK(snap.session.distraction_count == 1, "Phone distraction episode increments once on first entry");
    CHECK(snap.session.phone_distraction_count == 1, "Phone distraction count increments once on first active phone_sync");
    CHECK(snap.focus_state == FocusState::PHONE_IN_USE, "Phone signal updates focus state");

    sig.timestamp_ms += 200;
    store.setPhoneSignal(sig);
    behavior.runOnce(ctx);
    snap = store.snapshot();
    CHECK(snap.session.distraction_count == 1, "Same phone distraction episode does not keep incrementing");
    CHECK(snap.session.phone_distraction_count == 1,
          "Repeated active phone_sync heartbeat does not increment phone distraction count");

    sig.timestamp_ms += 200;
    sig.usage_state = PhoneUsageState::PLAYING;
    store.setPhoneSignal(sig);
    behavior.runOnce(ctx);
    snap = store.snapshot();
    CHECK(snap.session.distraction_count == 1, "Phone usage subtype changes do not create a new episode");
    CHECK(snap.session.phone_distraction_count == 1,
          "Phone usage subtype change in same active episode does not increment phone distraction count");

    sig.timestamp_ms += 200;
    sig.active = false;
    store.setPhoneSignal(sig);
    behavior.runOnce(ctx);
    store.updateFocusState(FocusState::FOCUSED, sig.timestamp_ms + 100, "focus back");

    const int64_t frame_base = sig.timestamp_ms + 1000;
    for (int i = 0; i < 4; ++i) {
        VisionResult result;
        result.timestamp_ms = frame_base + i * 1000;
        result.frame_quality = {FrameQualityLevel::GOOD, 0.95f, true, "quality"};
        result.presence = {Presence::AWAY, 0.95f, true};
        result.presence_conf = 0.95f;
        result.face = {FaceState::DETECTED, 0.95f, true};
        result.debug_note = "away";
        CHECK(vision_queue.push(result), "Away frame can be queued");
        behavior.runOnce(ctx);
    }

    snap = store.snapshot();
    CHECK(snap.focus_state == FocusState::AWAY, "Vision away triggers distraction state");
    CHECK(snap.session.distraction_count == 2, "Vision away episode increments once when entering distraction");

    VisionResult sustained;
    sustained.timestamp_ms = frame_base + 5000;
    sustained.frame_quality = {FrameQualityLevel::GOOD, 0.95f, true, "quality"};
    sustained.presence = {Presence::AWAY, 0.95f, true};
    sustained.presence_conf = 0.95f;
    sustained.face = {FaceState::DETECTED, 0.95f, true};
    CHECK(vision_queue.push(sustained), "Sustained away frame queued");
    behavior.runOnce(ctx);

    CHECK(store.snapshot().session.distraction_count == 2, "Sustained away does not add repeated distraction count");
}

// ============================================================================
// Test 14.1: Vision frame quality is visible through SystemStateStore
// ============================================================================
void test_behavior_updates_frame_quality_snapshot() {
    std::cout << "\n[Test 14.1] BehaviorEngine updates frame quality snapshot\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(2, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    const auto initial = store.snapshot();
    SessionInfo s;
    s.active = true;
    s.session_id = "frame_quality_session";
    s.start_ms = initial.snapshot_ms;
    s.target_duration_ms = 25 * 60000;
    s.remaining_ms = s.target_duration_ms;
    s.calibrating = false;
    s.calibration_completed = true;
    store.updateSession(s);
    store.updateFocusState(FocusState::FOCUSED, initial.snapshot_ms, "test focused");

    VisionResult result;
    result.timestamp_ms = initial.snapshot_ms + 1000;
    result.frame_quality.level = FrameQualityLevel::GOOD;
    result.frame_quality.score = 0.73f;
    result.frame_quality.reason = "test_quality";
    result.frame_quality.valid = true;
    CHECK(vision_queue.push(result), "Frame quality test queues a vision result");

    behavior.runOnce(ctx);

    auto snap = store.snapshot();
    CHECK(snap.frame_quality.score > 0.72f && snap.frame_quality.score < 0.74f,
          "BehaviorEngine writes frame quality score to store");
    CHECK(snap.frame_quality.reason == "test_quality",
          "BehaviorEngine writes frame quality reason to store");
    CHECK(snap.frame_quality.guidance_needed == false,
          "BehaviorEngine writes frame quality guidance flag to store");
}

// ============================================================================
// Test 14.2: Session start clears stale pending phone_sync
// ============================================================================
void test_session_start_clears_stale_phone_signal() {
    std::cout << "\n[Test 14.2] Session start clears stale pending phone signal\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    PhoneUsageSignal stale;
    stale.active = true;
    stale.timestamp_ms = store.snapshot().snapshot_ms;
    stale.distraction_level = 3;
    stale.usage_state = PhoneUsageState::PLAYING;
    store.setPhoneSignal(stale);

    SessionController controller(store, sqlite);
    auto started = controller.startSession(25, "test");
    CHECK(started.success, "Session start succeeds with stale phone signal present");

    PhoneUsageSignal consumed;
    CHECK(!store.consumePhoneSignal(consumed), "Session start clears stale pending phone signal");
}

// ========================================================================
// Test 14.5: Paused session freezes behavior progression and counters
// ========================================================================
void test_behavior_pause_freezes_elapsed_and_counter_updates() {
    std::cout << "\n[Test 14.5] Paused session freezes behavior progression\n";

    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext ctx;
    sqlite.init(ctx);

    BoundedQueue<VisionResult> vision_queue(4, false);
    BehaviorEngine behavior(vision_queue, store, sqlite);
    behavior.init(ctx);

    const auto initial = store.snapshot();
    SessionInfo s;
    s.active = true;
    s.paused = true;
    s.session_id = "paused_integration_session";
    s.start_ms = initial.snapshot_ms - 8000;
    s.elapsed_ms = 8000;
    s.remaining_ms = 5000;
    s.calibrating = false;
    s.calibration_completed = true;
    s.target_duration_ms = 5000;
    store.updateSession(s);

    VisionResult queued;
    queued.timestamp_ms = initial.snapshot_ms;
    CHECK(vision_queue.push(queued), "Paused run can queue a stale frame");

    PhoneUsageSignal sig;
    sig.active = true;
    sig.timestamp_ms = initial.snapshot_ms;
    sig.distraction_level = 2;
    store.setPhoneSignal(sig);

    behavior.runOnce(ctx);

    auto snap = store.snapshot();
    CHECK(snap.session.active == true, "Paused session does not auto-complete timer");
    CHECK(snap.session.paused == true, "Pause marker remains");
    CHECK(snap.session.elapsed_ms == 8000, "Paused session keeps elapsed_ms unchanged");
    CHECK(snap.session.remaining_ms == 5000, "Paused session keeps remaining_ms unchanged");
    CHECK(snap.session.phone_distraction_count == 0, "Paused session ignores phone signals");
    CHECK(snap.last_report.session_id.empty(), "Paused session does not emit timer completion report");
    CHECK(!vision_queue.tryPop().has_value(), "Paused session drops queued vision results");
}

int main() {
    std::cout << "=== Module Integration Tests ===\n";

    test_session_phase_helper();
    test_behavior_to_policy_contract();
    test_policy_to_device_contract();
    test_session_info_contract();
    test_snapshot_to_gatt_contract();
    test_mode_sync_contract();
    test_policy_skips_non_reminder_states_without_side_effects();
    test_report_contract();
    test_session_lifecycle();
    test_focus_state_enum_consistency();
    test_timer_completion_report_snapshot();
    test_away_timeout_uses_authoritative_session_end();
    test_calibration_completion_push_fields();
    test_calibration_placeholder_without_frames();
    test_status_page_local_lifecycle_persists_session();
    test_status_page_syncs_with_session_snapshot();
    test_status_page_precheck_view_model_uses_camera_guidance();
    test_session_precheck_owner_stays_internal_to_start_surface();
    test_status_page_does_not_skip_app_owned_precheck();
    test_status_page_alert_copy_mapping();
    test_uimanager_auto_switch_to_status_on_session_start();
    test_uimanager_phone_screen_route_bypasses_board_screen_hardware();
    test_session_controller_lifecycle_and_recovery();
    test_session_controller_end_focus_accumulation();
    test_session_controller_end_refreshes_stale_elapsed();
    test_session_controller_end_is_idempotent();
    test_session_controller_end_is_idempotent_under_concurrency();
    test_stale_session_update_cannot_resurrect_ended_session();
    test_session_controller_precheck_does_not_count_as_focus_time();
    test_phone_signal_updates_focus_state_immediately();
    test_inactive_phone_sync_clears_stale_pressure();
    test_distraction_count_is_episode_based();
    test_behavior_updates_frame_quality_snapshot();
    test_session_start_clears_stale_phone_signal();
    test_behavior_pause_freezes_elapsed_and_counter_updates();

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
