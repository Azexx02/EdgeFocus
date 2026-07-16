#include "ReminderPolicy.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "app/SessionPhase.h"
#include "device/DeviceManager.h"

#include "storage/StorageEvent.h"
#include <algorithm>
#include <sstream>
#include <vector>

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

CommandLevel commandLevelFromRiskScore(const BehaviorContext& ctx) {
    const int risk_score = std::clamp(ctx.risk_score, 0, 100);
    if (risk_score >= 80) return CommandLevel::LEVEL_3;
    if (risk_score >= 45) return CommandLevel::LEVEL_2;
    if (risk_score > 0) return CommandLevel::LEVEL_1;
    return CommandLevel::NONE;
}

bool hasBehaviorSignal(const BehaviorContext& ctx) {
    return ctx.vision_signal || ctx.phone_signal;
}

bool isPureLearningPhoneSignal(const BehaviorContext& ctx) {
    return ctx.phone_signal &&
           !ctx.vision_signal &&
           ctx.phone_usage_state == static_cast<int>(PhoneUsageState::LEARNING);
}

std::string messageFromContext(const BehaviorContext& ctx, CommandLevel level) {
    if (ctx.phone_signal && !ctx.vision_signal) {
        if (ctx.phone_usage_state == static_cast<int>(PhoneUsageState::PLAYING) ||
            level == CommandLevel::LEVEL_3) {
            return "High phone distraction detected";
        }
        return "Phone distraction detected";
    }
    if (level == CommandLevel::LEVEL_3) {
        return "Strong focus reminder";
    }
    if (level == CommandLevel::LEVEL_2) {
        return "Focus reminder";
    }
    return "Gentle focus reminder";
}

bool isPhoneRoute(const Preferences& prefs, NotificationChannel channel) {
    if (channel == NotificationChannel::SCREEN || channel == NotificationChannel::LIGHT) {
        return prefs.screen_output_route == "phone";
    }
    if (channel == NotificationChannel::SOUND) {
        return prefs.sound_output_route == "phone";
    }
    return false;
}

std::string notificationChannelName(NotificationChannel channel) {
    switch (channel) {
        case NotificationChannel::SCREEN:
        case NotificationChannel::LIGHT:
            return "SCREEN";
        case NotificationChannel::SOUND:
            return "SOUND";
        case NotificationChannel::BAND:
            return "BAND";
        default:
            return "";
    }
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

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    auto cmd = processCurrentContext(now_ms);
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
        std::vector<DeviceAction> board_actions;
        std::vector<std::string> phone_actions;
        std::vector<NotificationChannel> attempted_channels;
        board_actions.reserve(actions.size());
        phone_actions.reserve(actions.size());
        attempted_channels.reserve(actions.size());
        for (const auto& action : actions) {
            attempted_channels.push_back(action.channel);
            if (isPhoneRoute(snap.preferences, action.channel)) {
                phone_actions.push_back(notificationChannelName(action.channel));
            } else {
                board_actions.push_back(action);
            }
        }
        const int attempted_mask = notificationChannelsToMask(attempted_channels);
        DeviceStatus device_status = snap.device;
        device_status.last_attempted_channels_mask = attempted_mask;
        // Delivery is confirmed asynchronously: board channels update after the
        // hardware action completes, and App-routed channels update on event_ack.
        device_status.last_delivered_channels_mask = 0;
        store_.updateDeviceStatus(device_status);
        for (const auto& action : board_actions) {
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
             << "\"phone_actions\":[";
        for (size_t i = 0; i < phone_actions.size(); ++i) {
            if (i > 0) push << ",";
            push << "\"" << phone_actions[i] << "\"";
        }
        push << "],"
             << "\"sound_volume\":" << after.preferences.sound_volume << ","
             << "\"since_ms\":" << now_ms << ","
             << "\"reminder_count\":" << after.session.reminder_count
             << "}}";
        store_.pushEvent(push.str());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void ReminderPolicy::shutdown(AppContext& /*ctx*/) {
    LOGI("ReminderPolicy", "Shutdown");
}

std::optional<InterventionCommand> ReminderPolicy::processCurrentContext(int64_t now_ms) {
    auto snapshot = store_.snapshot();
    const auto& behavior_context = snapshot.behavior_context;
    if (snapshot.session.active && deriveSessionPhase(snapshot.session) != SessionPhase::FOCUSING) {
        return std::nullopt;
    }

    const CommandLevel target_level = commandLevelFromRiskScore(behavior_context);
    if (target_level == CommandLevel::NONE ||
        !hasBehaviorSignal(behavior_context) ||
        isPureLearningPhoneSignal(behavior_context)) {
        last_level_ = CommandLevel::NONE;
        store_.clearReminder();
        return std::nullopt;
    }

    if (last_reminder_time_ms_ > 0 && (now_ms - last_reminder_time_ms_) < current_cooling_ms_) {
        return std::nullopt;
    }

    InterventionCommand cmd;
    cmd.timestamp_ms = now_ms;
    cmd.risk_score = behavior_context.risk_score;
    cmd.phone_distraction_level = behavior_context.phone_distraction_level;
    cmd.reason = behavior_context.reason;
    cmd.level = target_level;
    cmd.message = messageFromContext(behavior_context, target_level);

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
