#include "policy/ReminderPolicy.h"
#include "app/SystemStateStore.h"
#include "app/AppContext.h"
#include "device/DeviceManager.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace edgefocus;

namespace {

void makeFocusedSession(SystemStateStore& store) {
    SessionInfo session;
    session.active = true;
    session.calibration_completed = true;
    session.session_id = "policy_ctx_session";
    store.updateSession(session);
}

void setContext(SystemStateStore& store, int level, int risk = 20, bool phone_signal = true) {
    BehaviorContext ctx;
    ctx.phone_signal = phone_signal;
    ctx.vision_signal = false;
    ctx.phone_distraction_level = phone_signal ? level : -1;
    ctx.phone_usage_state = phone_signal ? static_cast<int>(PhoneUsageState::IN_USE) : -1;
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
    assert(normal_cmd->message == silent_cmd->message);
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

void test_phone_learning_does_not_issue_reminder() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store(":memory:");
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    assert(sqlite_store.init(ctx));
    policy.init(ctx);

    auto cmd = policy.processState(FocusState::PHONE_IN_LEARNING, 1000);
    assert(!cmd.has_value());
    assert(store.snapshot().session.reminder_count == 0);
    assert(store.snapshot().reminder.level == CommandLevel::NONE);

    for (int i = 0; i < 4; ++i) {
        sqlite_store.runOnce(ctx);
    }
    const auto events = sqlite_store.queryEvents(store.snapshot().session.session_id);
    for (const auto& event : events) {
        assert(event.type != StorageEventType::ReminderIssued);
    }
}

void test_visual_state_uses_risk_score_for_command_level() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);

    setContext(store, 0, 20);
    auto low_cmd = policy.processState(FocusState::LOOKING_AWAY, 1000);
    assert(low_cmd.has_value());
    assert(low_cmd->level == CommandLevel::LEVEL_1);

    SystemStateStore medium_store;
    makeFocusedSession(medium_store);
    SQLiteStore medium_sqlite_store;
    DeviceManager medium_dev_mgr(medium_store);
    ReminderPolicy medium_policy(medium_store, medium_sqlite_store, medium_dev_mgr);

    setContext(medium_store, 1, 60);
    auto medium_cmd = medium_policy.processState(FocusState::LOOKING_AWAY, 1000);
    assert(medium_cmd.has_value());
    assert(medium_cmd->level == CommandLevel::LEVEL_2);

    SystemStateStore high_store;
    makeFocusedSession(high_store);
    SQLiteStore high_sqlite_store;
    DeviceManager high_dev_mgr(high_store);
    ReminderPolicy high_policy(high_store, high_sqlite_store, high_dev_mgr);

    setContext(high_store, 2, 80);
    auto high_cmd = high_policy.processState(FocusState::LOOKING_AWAY, 1000);
    assert(high_cmd.has_value());
    assert(high_cmd->level == CommandLevel::LEVEL_3);
}

void test_sleepy_state_uses_standard_command_level() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);

    setContext(store, 0, 80, false);
    auto cmd = policy.processState(FocusState::SLEEPY_OR_SLUMPED, 1000);
    assert(cmd.has_value());
    assert(cmd->level == CommandLevel::LEVEL_3);
    assert(cmd->message == "Take a short break and sit upright");
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
    assert(snap.device.last_attempted_channels_mask ==
           (kNotificationChannelScreenMask | kNotificationChannelSoundMask));
    assert(snap.device.last_delivered_channels_mask ==
           (kNotificationChannelScreenMask | kNotificationChannelSoundMask));
    assert(push.find("\"phone_actions\":[\"SCREEN\",\"SOUND\"]") != std::string::npos);
    assert(push.find("\"sound_volume\":0.42") != std::string::npos);
}

void test_low_confidence_does_not_create_reminder_side_effects() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store(":memory:");
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    assert(sqlite_store.init(ctx));
    policy.init(ctx);

    setContext(store, 3, 95);
    auto no_cmd = policy.processState(FocusState::LOW_CONFIDENCE, 1000);
    assert(!no_cmd.has_value());

    auto after_low_conf = store.snapshot();
    assert(after_low_conf.session.reminder_count == 0);
    assert(after_low_conf.reminder.level == CommandLevel::NONE);
    assert(after_low_conf.reminder.message.empty());

    auto real_cmd = policy.processState(FocusState::PHONE_IN_USE, 2000);
    assert(real_cmd.has_value());
    assert(real_cmd->level == CommandLevel::LEVEL_3);

    for (int i = 0; i < 4; ++i) {
        sqlite_store.runOnce(ctx);
    }

    const auto events = sqlite_store.queryEvents(store.snapshot().session.session_id);
    int reminder_events = 0;
    for (const auto& event : events) {
        if (event.type == StorageEventType::ReminderIssued) {
            reminder_events++;
        }
    }
    assert(reminder_events == 1);
}

void test_away_does_not_create_reminder_side_effects() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store(":memory:");
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    assert(sqlite_store.init(ctx));
    policy.init(ctx);

    auto no_cmd = policy.processState(FocusState::AWAY, 1000);
    assert(!no_cmd.has_value());

    auto after_away = store.snapshot();
    assert(after_away.session.reminder_count == 0);
    assert(after_away.reminder.level == CommandLevel::NONE);
    assert(after_away.reminder.message.empty());

    auto real_cmd = policy.processState(FocusState::LOOKING_AWAY, 2000);
    assert(real_cmd.has_value());
    assert(real_cmd->level == CommandLevel::LEVEL_1);

    for (int i = 0; i < 4; ++i) {
        sqlite_store.runOnce(ctx);
    }

    const auto events = sqlite_store.queryEvents(store.snapshot().session.session_id);
    int reminder_events = 0;
    for (const auto& event : events) {
        if (event.type == StorageEventType::ReminderIssued) {
            reminder_events++;
        }
    }
    assert(reminder_events == 1);
}

void test_non_reminder_state_clears_stale_active_reminder() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store(":memory:");
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    AppContext ctx;
    assert(sqlite_store.init(ctx));
    policy.init(ctx);

    setContext(store, 3, 95);
    auto phone = policy.processState(FocusState::PHONE_IN_USE, 1000);
    assert(phone.has_value());
    assert(store.snapshot().reminder.level == CommandLevel::LEVEL_3);

    auto no_cmd = policy.processState(FocusState::LOW_CONFIDENCE, 7000);
    assert(!no_cmd.has_value());
    assert(store.snapshot().session.reminder_count == 1);
    assert(store.snapshot().reminder.level == CommandLevel::NONE);
    assert(store.snapshot().reminder.message.empty());
}

void test_home_mode_does_not_shorten_cooldown() {
    SystemStateStore store;
    makeFocusedSession(store);
    SQLiteStore sqlite_store;
    DeviceManager dev_mgr(store);
    ReminderPolicy policy(store, sqlite_store, dev_mgr);
    policy.getModeManager().setMode(SystemMode::HOME);

    auto first = policy.processState(FocusState::LOOKING_AWAY, 1000);
    assert(first.has_value());
    assert(first->level == CommandLevel::LEVEL_1);

    auto during_cooldown = policy.processState(FocusState::LOOKING_AWAY, 4000);
    assert(!during_cooldown.has_value());

    auto after_cooldown = policy.processState(FocusState::LOOKING_AWAY, 7000);
    assert(after_cooldown.has_value());
}

int main() {
    test_playing_state_not_fixed_to_level3();
    test_playing_state_can_be_low_when_risk_is_low();
    test_use_state_can_escalate_to_level3();
    test_silent_does_not_change_level_for_same_context();
    test_run_once_uses_configured_preference_channels();
    test_phone_learning_does_not_issue_reminder();
    test_visual_state_uses_risk_score_for_command_level();
    test_sleepy_state_uses_standard_command_level();
    test_user_ack_increases_cooldown();
    test_risk_and_distraction_context_in_command();
    test_phone_output_routes_do_not_execute_board_screen_or_sound();
    test_low_confidence_does_not_create_reminder_side_effects();
    test_away_does_not_create_reminder_side_effects();
    test_non_reminder_state_clears_stale_active_reminder();
    test_home_mode_does_not_shorten_cooldown();

    std::cout << "All reminder policy tests passed!\n";
    return 0;
}
