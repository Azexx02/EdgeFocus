#include "ReminderPolicy.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "app/SessionPhase.h"
#include "device/DeviceManager.h"

#include "storage/StorageEvent.h"
#include <algorithm>
#include <sstream>

namespace edgefocus {

namespace {
std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

CommandLevel phoneContextLevel(const BehaviorContext& ctx) {
    const int risk_score = std::clamp(ctx.risk_score, 0, 100);
    const int distraction_level = std::clamp(ctx.phone_distraction_level, 1, 3);

    if (!ctx.phone_signal) {
        if (risk_score >= 95) return CommandLevel::LEVEL_3;
        if (risk_score >= 80) return CommandLevel::LEVEL_2;
        return CommandLevel::LEVEL_1;
    }

    if (distraction_level >= 3 || risk_score >= 90) return CommandLevel::LEVEL_3;
    if (distraction_level >= 2 || risk_score >= 70) return CommandLevel::LEVEL_2;
    return CommandLevel::LEVEL_1;
}

} // namespace

ReminderPolicy::ReminderPolicy(SystemStateStore& store, SQLiteStore& sqlite_store, DeviceManager& device_mgr)
    : Module("ReminderPolicy"), store_(store), sqlite_store_(sqlite_store), device_mgr_(device_mgr) {}

bool ReminderPolicy::init(AppContext& /*ctx*/) {
    LOGI("ReminderPolicy", "Init ReminderPolicy");
    return true;
}

void ReminderPolicy::runOnce(AppContext& /*ctx*/) {
    SystemSnapshot snap = store_.snapshot();

    // Only process reminders when phase is focusing.
    if (deriveSessionPhase(snap.session) != SessionPhase::FOCUSING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return;
    }

    // Call processState with current focus state
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    auto cmd = processState(snap.focus_state, now_ms);
    if (cmd.has_value()) {
        LOGI("ReminderPolicy", "Issued reminder: level=%d msg=%s",
             static_cast<int>(cmd->level), cmd->message.c_str());
        auto actions = notifier_.route(
            ReminderIntent{cmd->level, cmd->message, cmd->timestamp_ms},
            mode_manager_.getMode(),
            NotificationPreferences{
                notificationMaskToChannels(snap.preferences.normal_channels_mask),
                notificationMaskToChannels(snap.preferences.silent_channels_mask)
            });
        for (const auto& action : actions) {
            device_mgr_.issueAction(action);
        }

        // Router-driven actions are the runtime dispatch path.
        // DeviceManager::issueCommand remains available as a compatibility API
        // for direct callers and tests, but is not invoked here.

        auto after = store_.snapshot();
        std::ostringstream push;
        push << "{\"cmd\":\"push_alert\",\"data\":{"
             << "\"level\":" << static_cast<int>(cmd->level) << ","
             << "\"message\":\"" << jsonEscape(cmd->message) << "\","
             << "\"risk_score\":" << after.behavior_context.risk_score << ","
             << "\"phone_distraction_level\":" << after.behavior_context.phone_distraction_level << ","
             << "\"vision_signal\":" << (after.behavior_context.vision_signal ? "true" : "false") << ","
             << "\"phone_signal\":" << (after.behavior_context.phone_signal ? "true" : "false") << ","
             << "\"reason\":\"" << jsonEscape(after.behavior_context.reason) << "\","
             << "\"since_ms\":" << now_ms << ","
             << "\"focus_state\":" << static_cast<int>(snap.focus_state) << ","
             << "\"reminder_count\":" << after.session.reminder_count
             << "}}";
        store_.pushEvent(push.str());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void ReminderPolicy::shutdown(AppContext& /*ctx*/) {
    LOGI("ReminderPolicy", "Shutdown");
}

std::optional<InterventionCommand> ReminderPolicy::processState(FocusState state, int64_t now_ms) {
    auto snapshot = store_.snapshot();
    const auto& behavior_context = snapshot.behavior_context;
    if (snapshot.session.active && deriveSessionPhase(snapshot.session) != SessionPhase::FOCUSING) {
        return std::nullopt;
    }

    if (state == FocusState::FOCUSED || state == FocusState::PAUSED ||
        state == FocusState::CALIBRATING || state == FocusState::PHONE_IN_LEARNING) {
        last_level_ = CommandLevel::NONE;
        return std::nullopt;
    }

    SystemMode mode = mode_manager_.getMode();

    int64_t effective_cooling = current_cooling_ms_;
    if (mode == SystemMode::HOME) {
        effective_cooling /= 2; // Shorter cooling in HOME mode (STRICT)
    }

    if (last_reminder_time_ms_ > 0 && (now_ms - last_reminder_time_ms_) < effective_cooling) {
        return std::nullopt;
    }

    InterventionCommand cmd;
    cmd.timestamp_ms = now_ms;
    cmd.risk_score = behavior_context.risk_score;
    cmd.phone_distraction_level = behavior_context.phone_distraction_level;
    cmd.reason = behavior_context.reason;
    CommandLevel target_level = CommandLevel::LEVEL_1; // default

    if (state == FocusState::LOOKING_AWAY) {
        target_level = CommandLevel::LEVEL_1;
        cmd.message = "Need glance back to screen";
    } else if (state == FocusState::SLEEPY_OR_SLUMPED) {
        target_level = CommandLevel::REST_SUGGESTION;
        cmd.message = "Take a short break and sit upright";
    } else if (state == FocusState::PHONE_IN_USE) {
        target_level = phoneContextLevel(behavior_context);
        cmd.message = "Phone usage detected";
    } else if (state == FocusState::PHONE_IN_PLAYING) {
        target_level = phoneContextLevel(behavior_context);
        cmd.message = "High phone distraction detected";
    } else if (state == FocusState::LOW_CONFIDENCE) {
        target_level = CommandLevel::NONE;
        cmd.message = "Vision signal unreliable";
    } else if (state == FocusState::AWAY) {
        target_level = CommandLevel::NONE;
        cmd.message = "Long away from desk, no reminder";
    } else {
        return std::nullopt;
    }

    // SILENT mode only changes the reminder medium marker, not policy severity.
    if (mode == SystemMode::SILENT) {
        if (target_level == CommandLevel::LEVEL_3 || target_level == CommandLevel::LEVEL_2) {
            cmd.message = "[Silent] " + cmd.message;
        }
    }

    cmd.level = target_level;
    cmd.risk_score = behavior_context.risk_score;
    cmd.phone_distraction_level = behavior_context.phone_distraction_level;
    cmd.reason = behavior_context.reason;

    last_level_ = cmd.level;
    last_reminder_time_ms_ = now_ms;

    // Reset cooling after a reminder is sent
    current_cooling_ms_ = base_cooling_ms_;

    // Update SystemStateStore with the reminder
    store_.updateReminder(cmd.level, cmd.message, now_ms);
    store_.incrementReminder();

    // Log the reminder event to SQLiteStore
    StorageEvent ev;
    ev.session_id = store_.snapshot().session.session_id;
    ev.type = StorageEventType::ReminderIssued;
    ev.timestamp = now_ms;
    ev.data = "Level: " + std::to_string(static_cast<int>(cmd.level)) + ", Message: " + cmd.message;
    sqlite_store_.logEvent(ev);

    return cmd;
}

void ReminderPolicy::onUserAck(int64_t /*now_ms*/) {
    // Temporarily increase cooling time to reduce frequency
    current_cooling_ms_ = base_cooling_ms_ * 3;
}

} // namespace edgefocus
