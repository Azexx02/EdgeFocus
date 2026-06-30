#include "AppCommandService.h"

#include "app/Logger.h"
#include "app/SessionController.h"
#include "app/SessionPhase.h"
#include "behavior/EventDetector.h"
#include "policy/NotificationRouter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <sstream>

namespace edgefocus {

using json = nlohmann::json;

namespace {
int64_t monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t wallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

json sessionStateData(const SystemSnapshot& snap) {
    return {
        {"active", snap.session.active},
        {"paused", snap.session.paused},
        {"session_id", snap.session.session_id},
        {"focus_state", static_cast<int>(snap.focus_state)},
        {"elapsed_ms", snap.session.elapsed_ms},
        {"remaining_ms", snap.session.remaining_ms},
        {"target_duration_ms", snap.session.target_duration_ms},
        {"distraction_count", snap.session.distraction_count},
        {"reminder_count", snap.session.reminder_count},
        {"calibration_completed", snap.session.calibration_completed},
        {"session_phase", sessionPhaseToString(deriveSessionPhase(snap.session))}
    };
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

PhoneUsageState parsePhoneUsageState(const json& data, int level) {
    std::string raw;
    if (data.contains("phone_state")) {
        raw = data.value("phone_state", "");
    } else if (data.contains("usage_state")) {
        raw = data.value("usage_state", "");
    } else if (data.contains("state")) {
        raw = data.value("state", "");
    } else {
        raw = data.value("app_category", "");
    }

    raw = lowerCopy(raw);
    if (raw == "learning" || raw == "study" || raw == "education" ||
        raw == "phone_in_learning") {
        return PhoneUsageState::LEARNING;
    }
    if (raw == "playing" || raw == "game" || raw == "games" ||
        raw == "video" || raw == "short_video" || raw == "short-video" ||
        raw == "entertainment" || raw == "phone_in_playing") {
        return PhoneUsageState::PLAYING;
    }

    // Locked v3.4 contract still documents level 0 as learning. Keep this as
    // a backward-compatible fallback only; non-zero levels never decide playing.
    if (raw.empty() && level == 0) {
        return PhoneUsageState::LEARNING;
    }
    return PhoneUsageState::IN_USE;
}

int normalizePhoneDistractionLevel(int level) {
    if (level < 1) return 1;
    if (level > 3) return 3;
    return level;
}

int clampMinutes(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

int parseIntOrDefault(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

int parseNotificationMaskOrDefault(const std::string& value, int fallback) {
    return parseIntOrDefault(value, fallback);
}

int sanitizeNotificationChannelMask(int value) {
    int mask = 0;
    if (value & kNotificationChannelScreenMask) mask |= kNotificationChannelScreenMask;
    if (value & kNotificationChannelBandMask) mask |= kNotificationChannelBandMask;
    if (value & kNotificationChannelSoundMask) mask |= kNotificationChannelSoundMask;
    if (value & kNotificationChannelLightMask) mask |= kNotificationChannelLightMask;
    return mask;
}

NotificationChannelMask parseChannelMaskFromValue(const json& value) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument("normal/silent channels mask must be integer");
    }
    const int raw = value.get<int>();
    return sanitizeNotificationChannelMask(raw);
}

NotificationChannelMask parseChannelMaskFromNames(const json& value) {
    if (!value.is_array()) {
        throw std::invalid_argument("normal/silent channels must be array");
    }
    int mask = 0;
    for (const auto& item : value) {
        if (!item.is_string()) {
            throw std::invalid_argument("notification channel must be a string");
        }
        const auto name = lowerCopy(item.get<std::string>());
        if (name == "screen") {
            mask |= kNotificationChannelScreenMask;
        } else if (name == "band") {
            mask |= kNotificationChannelBandMask;
        } else if (name == "sound") {
            mask |= kNotificationChannelSoundMask;
        } else if (name == "light") {
            mask |= kNotificationChannelLightMask;
        } else {
            throw std::invalid_argument("unknown notification channel: " + name);
        }
    }
    return mask;
}

float parseFloatOrDefault(const std::string& value, float fallback) {
    try {
        return std::stof(value);
    } catch (...) {
        return fallback;
    }
}
} // namespace

AppCommandService::AppCommandService(SystemStateStore& store, SQLiteStore& sqlite_store,
                                     ReminderPolicy& reminder_policy, ModeManager& mode_mgr)
    : store_(store)
    , sqlite_store_(sqlite_store)
    , reminder_policy_(reminder_policy)
    , mode_mgr_(mode_mgr) {
    Preferences prefs = store_.snapshot().preferences;
    prefs.system_mode = parseIntOrDefault(sqlite_store_.getPreference("system_mode", std::to_string(prefs.system_mode)),
                                          prefs.system_mode);
    prefs.away_timeout_min = parseIntOrDefault(sqlite_store_.getPreference("away_timeout_min", std::to_string(prefs.away_timeout_min)),
                                               prefs.away_timeout_min);
    prefs.default_focus_duration_min = clampMinutes(
        parseIntOrDefault(sqlite_store_.getPreference("default_focus_duration_min",
                                                      std::to_string(prefs.default_focus_duration_min)),
                          prefs.default_focus_duration_min),
        5, 120);
    prefs.vibration_strength = parseFloatOrDefault(sqlite_store_.getPreference("vibration_strength", std::to_string(prefs.vibration_strength)),
                                                   prefs.vibration_strength);
    prefs.led_brightness = parseIntOrDefault(sqlite_store_.getPreference("led_brightness", std::to_string(prefs.led_brightness)),
                                             prefs.led_brightness);
    prefs.reminder_sensitivity = parseFloatOrDefault(sqlite_store_.getPreference("reminder_sensitivity", std::to_string(prefs.reminder_sensitivity)),
                                                     prefs.reminder_sensitivity);
    prefs.normal_channels_mask = sanitizeNotificationChannelMask(
        parseNotificationMaskOrDefault(
            sqlite_store_.getPreference("normal_channels_mask", std::to_string(prefs.normal_channels_mask)),
            prefs.normal_channels_mask));
    prefs.silent_channels_mask = sanitizeNotificationChannelMask(
        parseNotificationMaskOrDefault(
            sqlite_store_.getPreference("silent_channels_mask", std::to_string(prefs.silent_channels_mask)),
            prefs.silent_channels_mask));
    store_.updatePreferences(prefs);
    mode_mgr_.setMode(static_cast<SystemMode>(prefs.system_mode));

    DeviceStatus device = store_.snapshot().device;
    device.system_mode = static_cast<SystemMode>(prefs.system_mode);
    store_.updateDeviceStatus(device);

    SessionInfo session = store_.snapshot().session;
    session.away_timeout_ms = prefs.away_timeout_min * 60000;
    store_.updateSession(session);
}

std::string AppCommandService::makeResponse(int req_id, const json& data) {
    json resp = {{"status", "success"}, {"id", req_id}, {"data", data}};
    return resp.dump();
}

std::string AppCommandService::makeError(int req_id, const std::string& error_msg) {
    json resp = {{"status", "error"}, {"id", req_id}, {"error", error_msg}};
    return resp.dump();
}

std::string AppCommandService::makePush(const std::string& cmd, const json& data) {
    json resp = {{"cmd", cmd}, {"data", data}};
    return resp.dump();
}

std::string AppCommandService::handleCommand(const std::string& json_cmd) {
    json req;
    try {
        req = json::parse(json_cmd);
    } catch (const json::exception& e) {
        return makeError(0, std::string("Invalid JSON: ") + e.what());
    }

    std::string cmd = req.value("cmd", "");
    int req_id = req.value("id", 0);
    json data = req.contains("data") ? req["data"] : json::object();

    LOGI("AppCommandService", "Received command: %s (id=%d)", cmd.c_str(), req_id);

    try {
        if (cmd == "get_device_status")      return handleGetDeviceStatus(req_id);
        if (cmd == "get_current_state")      return handleGetCurrentState(req_id);
        if (cmd == "get_today_summary")      return handleGetTodaySummary(req_id);
        if (cmd == "get_last_report")        return handleGetLastReport(req_id);
        if (cmd == "session_control")        return handleSessionControl(data, req_id);
        if (cmd == "update_preferences")     return handleUpdatePreferences(data, req_id);
        if (cmd == "reminder_ack")           return handleReminderAck(data, req_id);
        if (cmd == "phone_sync")             return handlePhoneSync(data, req_id);
        if (cmd == "get_console_data")       return handleGetConsoleData(req_id);
        if (cmd == "session_feedback")       return handleSessionFeedback(data, req_id);
        if (cmd == "get_session_history")    return handleGetSessionHistory(data, req_id);
        if (cmd == "get_preferences")        return handleGetPreferences(req_id);
    } catch (const json::exception& e) {
        return makeError(req_id, std::string("Invalid command data: ") + e.what());
    } catch (const std::exception& e) {
        return makeError(req_id, std::string("Command failed: ") + e.what());
    }

    return makeError(req_id, "Unknown command: " + cmd);
}

std::string AppCommandService::handleGetDeviceStatus(int req_id) {
    auto snap = store_.snapshot();
    json data = {
        {"battery_percent", snap.device.battery_percent},
        {"system_mode", static_cast<int>(snap.device.system_mode)},
        {"camera_ok", snap.device.camera_ok},
        {"bracelet_connected", snap.device.bracelet_connected},
        {"is_session_active", snap.session.active}
    };
    return makeResponse(req_id, data);
}

std::string AppCommandService::handleGetCurrentState(int req_id) {
    auto snap = store_.snapshot();
    json data = {
        {"active", snap.session.active},
        {"paused", snap.session.paused},
        {"session_id", snap.session.session_id},
        {"focus_state", static_cast<int>(snap.focus_state)},
        {"elapsed_ms", snap.session.elapsed_ms},
        {"remaining_ms", snap.session.remaining_ms},
        {"target_duration_ms", snap.session.target_duration_ms},
        {"distraction_count", snap.session.distraction_count},
        {"reminder_count", snap.session.reminder_count},
        {"calibration_completed", snap.session.calibration_completed},
        {"session_phase", sessionPhaseToString(deriveSessionPhase(snap.session))}
    };
    return makeResponse(req_id, data);
}

std::string AppCommandService::handleGetTodaySummary(int req_id) {
    auto snap = store_.snapshot();
    json data = {
        {"total_seconds", snap.today.total_seconds},
        {"focus_seconds", snap.today.focus_seconds},
        {"sessions", snap.today.sessions},
        {"events", snap.today.events},
        {"phone_distraction_count", snap.today.phone_distraction_count}
    };
    return makeResponse(req_id, data);
}

std::string AppCommandService::handleGetLastReport(int req_id) {
    auto last = sqlite_store_.queryLastCompletedSession();
    if (last.session_id.empty()) {
        return makeResponse(req_id, nullptr);
    }
    json data = {
        {"session_id", last.session_id},
        {"total_duration", last.total_duration},
        {"focus_duration", last.focus_duration},
        {"distraction_count", last.distraction_count},
        {"reminder_count", last.reminder_count},
        {"phone_distraction_count", last.phone_distraction_count},
        {"feedback", last.feedback}
    };
    return makeResponse(req_id, data);
}

std::string AppCommandService::handleSessionControl(const json& data, int req_id) {
    std::string action = data.value("action", "");
    auto snap = store_.snapshot();

    if (action == "start") {
        int target_min = data.value("target_duration_min", 0);
        if (target_min <= 0 && data.contains("target_duration_ms")) {
            target_min = static_cast<int>((data["target_duration_ms"].get<int64_t>() + 59999) / 60000);
        }
        if (target_min <= 0 && data.contains("duration_ms")) {
            target_min = static_cast<int>((data["duration_ms"].get<int64_t>() + 59999) / 60000);
        }
        if (target_min <= 0) {
            target_min = snap.preferences.default_focus_duration_min;
        }
        SessionController controller(store_, sqlite_store_);
        auto started = controller.startSession(target_min, "app");
        const auto& s = started.session;
        LOGI("AppCommandService", "Session started: %s", s.session_id.c_str());
        json resp = {
            {"success", started.success},
            {"already_active", started.already_active},
            {"session_id", s.session_id},
            {"remaining_ms", s.remaining_ms},
            {"target_duration_ms", s.target_duration_ms},
            {"focus_state", static_cast<int>(store_.snapshot().focus_state)},
            {"calibration_completed", s.calibration_completed},
            {"session_phase", sessionPhaseToString(deriveSessionPhase(s))}
        };
        return makeResponse(req_id, resp);

    } else if (action == "skip_precheck" || action == "skip_calibration") {
        if (!snap.session.active) {
            json resp = {{"success", false}, {"message", "No active session"}};
            return makeResponse(req_id, resp);
        }
        SessionController controller(store_, sqlite_store_);
        bool ok = controller.skipPrecheck("app_skip_precheck");
        auto after = store_.snapshot();
        json resp = sessionStateData(after);
        resp["success"] = ok;
        store_.pushEvent(makePush("push_state", sessionStateData(after)));
        return makeResponse(req_id, resp);

    } else if (action == "pause") {
        if (!snap.session.active || snap.session.paused) {
            json resp = {{"success", false}, {"message", "No active session to pause"}};
            return makeResponse(req_id, resp);
        }
        SessionController controller(store_, sqlite_store_);
        bool ok = controller.pauseSession("User paused");
        auto after = store_.snapshot();
        json resp = sessionStateData(after);
        resp["success"] = ok;
        store_.pushEvent(makePush("push_state", sessionStateData(after)));
        return makeResponse(req_id, resp);

    } else if (action == "resume") {
        if (!snap.session.active || !snap.session.paused) {
            json resp = {{"success", false}, {"message", "No paused session to resume"}};
            return makeResponse(req_id, resp);
        }
        SessionController controller(store_, sqlite_store_);
        bool ok = controller.resumeSession("User resumed");
        auto after = store_.snapshot();
        json resp = sessionStateData(after);
        resp["success"] = ok;
        store_.pushEvent(makePush("push_state", sessionStateData(after)));
        return makeResponse(req_id, resp);

    } else if (action == "end") {
        if (!snap.session.active) {
            json resp = {{"success", false}, {"message", "No active session"}};
            return makeResponse(req_id, resp);
        }
        int64_t target_duration_ms = snap.session.target_duration_ms;
        SessionController controller(store_, sqlite_store_);
        auto ended = controller.endSession("user_ended");
        SessionReport report = ended.report;
        if (ended.success && !report.session_id.empty()) {
            json push_data = {
                {"total_duration", report.total_duration},
                {"focus_duration", report.focus_duration},
                {"distraction_count", report.distraction_count},
                {"reminder_count", report.reminder_count},
                {"phone_distraction_count", report.phone_distraction_count},
                {"session_id", report.session_id}
            };
            store_.pushEvent(makePush("push_session_end", push_data));
        }

        json resp = {
            {"success", ended.success},
            {"session_id", report.session_id},
            {"total_duration", report.total_duration},
            {"focus_duration", report.focus_duration},
            {"distraction_count", report.distraction_count},
            {"reminder_count", report.reminder_count},
            {"phone_distraction_count", report.phone_distraction_count},
            {"remaining_ms", 0},
            {"target_duration_ms", target_duration_ms}
        };
        return makeResponse(req_id, resp);
    }

    return makeError(req_id, "Unknown action: " + action);
}

std::string AppCommandService::handleUpdatePreferences(const json& data, int req_id) {
    Preferences prefs = store_.snapshot().preferences;

    if (data.contains("system_mode")) {
        int mode = data["system_mode"].get<int>();
        prefs.system_mode = mode;
        mode_mgr_.setMode(static_cast<SystemMode>(mode));
        DeviceStatus ds = store_.snapshot().device;
        ds.system_mode = static_cast<SystemMode>(mode);
        store_.updateDeviceStatus(ds);
    }
    if (data.contains("away_timeout_min")) {
        prefs.away_timeout_min = data["away_timeout_min"].get<int>();
        SessionInfo s = store_.snapshot().session;
        s.away_timeout_ms = prefs.away_timeout_min * 60000;
        store_.updateSession(s);
    }
    if (data.contains("default_focus_duration_min")) {
        prefs.default_focus_duration_min = clampMinutes(data["default_focus_duration_min"].get<int>(), 5, 120);
    }
    if (data.contains("vibration_strength")) {
        prefs.vibration_strength = data["vibration_strength"].get<float>();
    }
    if (data.contains("led_brightness")) {
        prefs.led_brightness = data["led_brightness"].get<int>();
    }
    if (data.contains("reminder_sensitivity")) {
        prefs.reminder_sensitivity = data["reminder_sensitivity"].get<float>();
    }
    if (data.contains("normal_channels_mask")) {
        prefs.normal_channels_mask = parseChannelMaskFromValue(data["normal_channels_mask"]);
    } else if (data.contains("normal_channels")) {
        prefs.normal_channels_mask = parseChannelMaskFromNames(data["normal_channels"]);
    }
    if (data.contains("silent_channels_mask")) {
        prefs.silent_channels_mask = parseChannelMaskFromValue(data["silent_channels_mask"]);
    } else if (data.contains("silent_channels")) {
        prefs.silent_channels_mask = parseChannelMaskFromNames(data["silent_channels"]);
    }

    store_.updatePreferences(prefs);

    sqlite_store_.savePreference("system_mode", std::to_string(prefs.system_mode));
    sqlite_store_.savePreference("away_timeout_min", std::to_string(prefs.away_timeout_min));
    sqlite_store_.savePreference("default_focus_duration_min", std::to_string(prefs.default_focus_duration_min));
    sqlite_store_.savePreference("vibration_strength", std::to_string(prefs.vibration_strength));
    sqlite_store_.savePreference("led_brightness", std::to_string(prefs.led_brightness));
    sqlite_store_.savePreference("reminder_sensitivity", std::to_string(prefs.reminder_sensitivity));
    sqlite_store_.savePreference("normal_channels_mask", std::to_string(prefs.normal_channels_mask));
    sqlite_store_.savePreference("silent_channels_mask", std::to_string(prefs.silent_channels_mask));

    json resp = {
        {"system_mode", prefs.system_mode},
        {"away_timeout_min", prefs.away_timeout_min},
        {"default_focus_duration_min", prefs.default_focus_duration_min},
        {"vibration_strength", prefs.vibration_strength},
        {"led_brightness", prefs.led_brightness},
        {"reminder_sensitivity", prefs.reminder_sensitivity},
        {"normal_channels_mask", prefs.normal_channels_mask},
        {"silent_channels_mask", prefs.silent_channels_mask},
        {"normal_channels", notificationMaskToNames(prefs.normal_channels_mask)},
        {"silent_channels", notificationMaskToNames(prefs.silent_channels_mask)}
    };
    return makeResponse(req_id, resp);
}

std::string AppCommandService::handleReminderAck(const json& /*data*/, int req_id) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    reminder_policy_.onUserAck(now_ms);
    json resp = {{"cooling_ms", 15000}};
    return makeResponse(req_id, resp);
}

std::string AppCommandService::handlePhoneSync(const json& data, int req_id) {
    bool is_in_use = data.value("is_in_use", false);
    int level = data.value("distraction_level", 3);
    std::string app_name = data.value("app_name", "");

    auto snap = store_.snapshot();

    if (app_name == "local_session_sync") {
        int duration_sec = data.value("duration_sec", 0);
        TodayStats today = snap.today;
        today.focus_seconds += duration_sec;
        store_.updateTodayStats(today);
        json resp = {
            {"focus_state", static_cast<int>(snap.focus_state)},
            {"action_taken", true}
        };
        return makeResponse(req_id, resp);
    }

    if (!is_in_use) {
        PhoneUsageSignal sig;
        sig.active = false;
        sig.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        store_.setPhoneSignal(sig);

        json resp = {
            {"focus_state", static_cast<int>(snap.focus_state)},
            {"action_taken", false}
        };
        return makeResponse(req_id, resp);
    }

    PhoneUsageSignal sig;
    sig.active = true;
    sig.distraction_level = normalizePhoneDistractionLevel(level);
    sig.usage_state = parsePhoneUsageState(data, level);
    sig.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    store_.setPhoneSignal(sig);

    json resp = {
        {"focus_state", static_cast<int>(snap.focus_state)},
        {"action_taken", true},
        {"distraction_count", snap.session.distraction_count},
        {"reminder_count", snap.session.reminder_count}
    };
    return makeResponse(req_id, resp);
}

std::string AppCommandService::handleGetConsoleData(int req_id) {
    auto snap = store_.snapshot();
    json data = {
        {"session", {
            {"active", snap.session.active},
            {"paused", snap.session.paused},
            {"session_id", snap.session.session_id},
            {"focus_state", static_cast<int>(snap.focus_state)},
            {"elapsed_ms", snap.session.elapsed_ms},
            {"target_duration_ms", snap.session.target_duration_ms},
            {"remaining_ms", snap.session.remaining_ms},
            {"distraction_count", snap.session.distraction_count},
            {"reminder_count", snap.session.reminder_count},
            {"calibration_completed", snap.session.calibration_completed},
            {"session_phase", sessionPhaseToString(deriveSessionPhase(snap.session))}
        }},
        {"device", {
            {"battery_percent", snap.device.battery_percent},
            {"system_mode", static_cast<int>(snap.device.system_mode)},
            {"camera_ok", snap.device.camera_ok},
            {"bracelet_connected", snap.device.bracelet_connected},
            {"band_last_alert_level", snap.device.band_last_alert_level},
            {"band_last_alert_pattern", snap.device.band_last_alert_pattern},
            {"band_write_attempt_count", snap.device.band_write_attempt_count},
            {"band_last_write_status", snap.device.band_last_write_status}
        }},
        {"reminder", {
            {"level", static_cast<int>(snap.reminder.level)},
            {"message", snap.reminder.message},
            {"since_ms", snap.reminder.since_ms}
        }},
        {"frame_quality", {
            {"score", snap.frame_quality.score},
            {"reason", snap.frame_quality.reason},
            {"guidance_needed", snap.frame_quality.guidance_needed}
        }},
        {"today", {
            {"total_seconds", snap.today.total_seconds},
            {"focus_seconds", snap.today.focus_seconds},
            {"sessions", snap.today.sessions},
            {"events", snap.today.events},
            {"phone_distraction_count", snap.today.phone_distraction_count}
        }}
    };
    return makeResponse(req_id, data);
}

std::string AppCommandService::handleSessionFeedback(const json& data, int req_id) {
    std::string session_id = data.value("session_id", "");
    std::string feedback = data.value("feedback", "");

    if (session_id.empty()) {
        return makeError(req_id, "Missing session_id");
    }
    if (feedback != "too_much" && feedback != "too_little" && feedback != "just_right") {
        return makeError(req_id, "Invalid feedback value: " + feedback);
    }

    if (!sqlite_store_.sessionExists(session_id)) {
        return makeError(req_id, "Session not found: " + session_id);
    }
    if (!sqlite_store_.saveFeedback(session_id, feedback)) {
        return makeError(req_id, "Failed to save feedback");
    }

    json resp = {{"success", true}};
    return makeResponse(req_id, resp);
}

std::string AppCommandService::handleGetSessionHistory(const json& data, int req_id) {
    std::string cursor = data.value("cursor", "");
    int limit = data.value("limit", 1);

    auto page = sqlite_store_.querySessionHistory(cursor, limit);

    json sessions = json::array();
    for (const auto& rec : page.sessions) {
        sessions.push_back({
            {"session_id", rec.session_id},
            {"start_time_ms", rec.start_time_ms},
            {"end_time_ms", rec.end_time_ms},
            {"total_duration", rec.total_duration},
            {"focus_duration", rec.focus_duration},
            {"distraction_count", rec.distraction_count},
            {"reminder_count", rec.reminder_count},
            {"phone_distraction_count", rec.phone_distraction_count},
            {"feedback", rec.feedback}
        });
    }

    json resp = {
        {"sessions", sessions},
        {"next_cursor", page.next_cursor},
        {"has_more", page.has_more}
    };
    return makeResponse(req_id, resp);
}

std::string AppCommandService::handleGetPreferences(int req_id) {
    auto snap = store_.snapshot();
    json resp = {
        {"system_mode", snap.preferences.system_mode},
        {"away_timeout_min", snap.preferences.away_timeout_min},
        {"default_focus_duration_min", snap.preferences.default_focus_duration_min},
        {"vibration_strength", snap.preferences.vibration_strength},
        {"led_brightness", snap.preferences.led_brightness},
        {"reminder_sensitivity", snap.preferences.reminder_sensitivity},
        {"normal_channels_mask", snap.preferences.normal_channels_mask},
        {"silent_channels_mask", snap.preferences.silent_channels_mask},
        {"normal_channels", notificationMaskToNames(snap.preferences.normal_channels_mask)},
        {"silent_channels", notificationMaskToNames(snap.preferences.silent_channels_mask)}
    };
    return makeResponse(req_id, resp);
}

} // namespace edgefocus
