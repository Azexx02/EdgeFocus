#include "policy/ReminderPolicy.h"
#include "app/SystemStateStore.h"
#include "app/AppContext.h"
#include "device/DeviceManager.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace edgefocus;

namespace {

void makeFocusedSession(SystemStateStore& store) {
    SessionInfo session;
    session.active = true;
    session.calibration_completed = true;
    session.session_id = "policy_ctx_session";
    store.updateSession(session);
}

void setContext(SystemStateStore& store, int level, int risk = 20) {
    BehaviorContext ctx;
    ctx.phone_signal = true;
    ctx.vision_signal = false;
    ctx.phone_distraction_level = level;
    ctx.risk_score = risk;
    ctx.reason = "test";
    store.updateBehaviorContext(ctx);
}

} // namespace

void test_playing_state_not_fixed_to_level3() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    policy.init(ctx);

    setContext(store, 1);
    auto cmd = policy.processState(FocusState::PHONE_IN_PLAYING, 1000);
    assert(cmd.has_value());
    assert(cmd->level == CommandLevel::LEVEL_1);
}

void test_use_state_can_escalate_to_level3() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    policy.init(ctx);

    setContext(store, 3, 95);
    auto cmd = policy.processState(FocusState::PHONE_IN_USE, 1000);
    assert(cmd.has_value());
    assert(cmd->level == CommandLevel::LEVEL_3);
}

void test_playing_state_can_be_low_when_risk_is_low() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    policy.init(ctx);

    setContext(store, 3, 20);
    auto cmd = policy.processState(FocusState::PHONE_IN_PLAYING, 1000);
    assert(cmd.has_value());
    assert(cmd->level == CommandLevel::LEVEL_1);
}

void test_silent_does_not_change_level_for_same_context() {
    SystemStateStore normal_store;
    makeFocusedSession(normal_store);
    SQLiteStore normal_sqlite;
    DeviceManager normal_dev_mgr(normal_store);
    ReminderPolicy normal_policy(normal_store, normal_sqlite, normal_dev_mgr);
    AppContext normal_ctx;
    normal_policy.init(normal_ctx);

    SystemStateStore silent_store;
    makeFocusedSession(silent_store);
    SQLiteStore silent_sqlite;
    DeviceManager silent_dev_mgr(silent_store);
    ReminderPolicy silent_policy(silent_store, silent_sqlite, silent_dev_mgr);
    silent_policy.getModeManager().setMode(SystemMode::SILENT);
    AppContext silent_ctx;
    silent_policy.init(silent_ctx);

    setContext(normal_store, 2, 70);
    setContext(silent_store, 2, 70);

    auto normal_cmd = normal_policy.processState(FocusState::PHONE_IN_USE, 1000);
    auto silent_cmd = silent_policy.processState(FocusState::PHONE_IN_USE, 1000);

    assert(normal_cmd.has_value());
    assert(silent_cmd.has_value());
    assert(normal_cmd->level == silent_cmd->level);
}

void test_run_once_uses_configured_preference_channels() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    policy.init(ctx);

    auto prefs = store.snapshot().preferences;
    prefs.normal_channels_mask = kNotificationChannelBandMask;
    prefs.silent_channels_mask = kNotificationChannelBandMask;
    store.updatePreferences(prefs);
    auto session = store.snapshot().session;
    session.calibration_completed = true;
    store.updateSession(session);
    setContext(store, 1);
    store.updateFocusState(FocusState::LOOKING_AWAY, 1000, "focused");

    policy.runOnce(ctx);
    dev_mgr.runOnce(ctx);

    assert(dev_mgr.screenFlashCount() == 0);
    assert(dev_mgr.lastBandAlertLevel() == static_cast<uint8_t>(CommandLevel::LEVEL_1));
}

void test_phone_learning_no_intervention() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);

    auto cmd = policy.processState(FocusState::PHONE_IN_LEARNING, 1000);
    assert(!cmd.has_value());
}

void test_looking_away_is_level1() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);

    auto cmd = policy.processState(FocusState::LOOKING_AWAY, 1000);
    assert(cmd.has_value());
    assert(cmd->level == CommandLevel::LEVEL_1);
}

void test_user_ack_increases_cooldown() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);

    auto cmd1 = policy.processState(FocusState::LOOKING_AWAY, 1000);
    assert(cmd1.has_value());

    policy.onUserAck(2000);
    assert(!policy.processState(FocusState::LOOKING_AWAY, 7000).has_value());
    assert(policy.processState(FocusState::LOOKING_AWAY, 18000).has_value());
}

void test_risk_and_distraction_context_in_command() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);

    setContext(store, 3, 92);
    auto cmd = policy.processState(FocusState::PHONE_IN_PLAYING, 1000);
    assert(cmd.has_value());
    assert(cmd->risk_score == 92);
    assert(cmd->phone_distraction_level == 3);
}

void test_phone_output_routes_do_not_execute_board_screen_or_sound() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    policy.init(ctx);

    auto prefs = store.snapshot().preferences;
    prefs.normal_channels_mask = kNotificationChannelScreenMask | kNotificationChannelSoundMask;
    prefs.board_output_route = "phone";
    prefs.screen_output_route = "phone";
    prefs.sound_output_route = "phone";
    prefs.sound_volume = 0.42f;
    store.updatePreferences(prefs);

    std::string push;
    store.setPushCallback([&push](const std::string& event_json) {
        push = event_json;
    });

    setContext(store, 1);
    store.updateFocusState(FocusState::LOOKING_AWAY, 1000, "focused");

    policy.runOnce(ctx);
    dev_mgr.runOnce(ctx);

    assert(dev_mgr.screenFlashCount() == 0);
    assert(dev_mgr.soundPlayCount() == 0);
    auto snap = store.snapshot();
    assert(snap.device.last_attempted_channels_mask == 0);
    assert(snap.device.last_delivered_channels_mask == 0);
    assert(push.find("\"phone_actions\":[\"SCREEN\",\"SOUND\"]") != std::string::npos);
    assert(push.find("\"sound_volume\":0.42") != std::string::npos);
}

int main() {
    test_playing_state_not_fixed_to_level3();
    test_playing_state_can_be_low_when_risk_is_low();
    test_use_state_can_escalate_to_level3();
    test_silent_does_not_change_level_for_same_context();
    test_run_once_uses_configured_preference_channels();
    test_phone_learning_no_intervention();
    test_looking_away_is_level1();
    test_user_ack_increases_cooldown();
    test_risk_and_distraction_context_in_command();
    test_phone_output_routes_do_not_execute_board_screen_or_sound();

    std::cout << "All reminder policy tests passed!\n";
    return 0;
}
