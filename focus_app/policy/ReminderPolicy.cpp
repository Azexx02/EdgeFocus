#include "ReminderPolicy.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"

#include "storage/StorageEvent.h"

namespace edgefocus {

ReminderPolicy::ReminderPolicy(SystemStateStore& store, SQLiteStore& sqlite_store) 
    : Module("ReminderPolicy"), store_(store), sqlite_store_(sqlite_store) {}

bool ReminderPolicy::init(AppContext& /*ctx*/) {
    LOGI("ReminderPolicy", "Init ReminderPolicy");
    return true;
}

void ReminderPolicy::runOnce(AppContext& /*ctx*/) {
    // Placeholder: W06 will add real state-driven reminder logic.
    // For now, clear any stale reminders periodically.
    static auto last_clear = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - last_clear > std::chrono::seconds(30)) {
        store_.clearReminder();
        last_clear = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void ReminderPolicy::shutdown(AppContext& /*ctx*/) {
    LOGI("ReminderPolicy", "Shutdown");
}

std::optional<InterventionCommand> ReminderPolicy::processState(FocusState state, int64_t now_ms) {
    if (state == FocusState::FOCUSED || state == FocusState::PAUSED ||
        state == FocusState::CALIBRATING) {
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
    CommandLevel target_level = CommandLevel::LEVEL_1; // default

    if (state == FocusState::LOOKING_AWAY) {
        target_level = CommandLevel::LEVEL_1;
        cmd.message = "视线好像离开学习区了，回来看看这一页吧。";
    } else if (state == FocusState::SLEEPY_OR_SLUMPED) {
        target_level = CommandLevel::REST_SUGGESTION;
        cmd.message = "看起来有点累，可以坐直一点，或者休息一下。";
    } else if (state == FocusState::PHONE_IN_USE) {
        target_level = CommandLevel::LEVEL_2;
        cmd.message = "学习期间手机正在使用，先放到一边吧。";
    } else if (state == FocusState::LOW_CONFIDENCE) {
        target_level = CommandLevel::NONE;
        cmd.message = "画面不太清楚，可以调整一下角度或补光。";
    } else if (state == FocusState::AWAY) {
        target_level = CommandLevel::NONE;
        cmd.message = "已暂停观察，回来后继续。";
    } else {
        return std::nullopt;
    }

    // SILENT mode downgrade
    if (mode == SystemMode::SILENT) {
        if (target_level == CommandLevel::LEVEL_3 || target_level == CommandLevel::LEVEL_2) {
            target_level = CommandLevel::LEVEL_1;
            cmd.message = "[Silent] " + cmd.message;
        }
    }

    cmd.level = target_level;

    last_level_ = cmd.level;
    last_reminder_time_ms_ = now_ms;

    // Reset cooling after a reminder is sent
    current_cooling_ms_ = base_cooling_ms_;

    // Update SystemStateStore with the reminder
    store_.updateReminder(cmd.level, cmd.message, now_ms);
    store_.incrementReminder();

    // Log the reminder event to SQLiteStore
    StorageEvent ev;
    ev.session_id = "current_session"; // Should be fetched properly
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
