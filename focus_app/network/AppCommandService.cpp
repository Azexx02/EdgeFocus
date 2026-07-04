#include "AppCommandService.h"

#include "app/Logger.h"
#include "app/SessionController.h"
#include "app/SessionPhase.h"
#include "behavior/EventDetector.h"
#include "network/PhoneSyncParser.h"
#include "device/GimbalMonitor.h"
#include "policy/NotificationRouter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <utility>

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

constexpr int kMinFocusDurationMin = 5;
constexpr int kMaxFocusDurationMin = 120;
constexpr int kMinAwayTimeoutMin = 1;
constexpr int kMaxAwayTimeoutMin = 180;
constexpr int kMaxSessionHistoryLimit = 100;
constexpr int64_t kMaxLocalSessionDurationSec = 24LL * 60LL * 60LL;
constexpr int kMaxUploadedCounter = 10000;

json outputCapabilityData(const SystemSnapshot& snap);
json cameraPreviewData(const SystemSnapshot& snap);
json behaviorContextData(const SystemSnapshot& snap);
json precheckFlowData(const SystemSnapshot& snap, bool gimbal_precheck_available);
bool gimbalPrecheckAvailable(const SystemSnapshot& snap, const GimbalMonitor* gimbal_monitor);

int publicFocusStateCode(FocusState state) {
    if (state == FocusState::PHONE_IN_LEARNING ||
        state == FocusState::PHONE_IN_PLAYING) {
        return static_cast<int>(FocusState::PHONE_IN_USE);
    }
    return static_cast<int>(state);
}

std::string phoneUsageStateToWire(int usage_state) {
    if (usage_state == static_cast<int>(PhoneUsageState::LEARNING)) return "learning";
    if (usage_state == static_cast<int>(PhoneUsageState::PLAYING)) return "playing";
    if (usage_state == static_cast<int>(PhoneUsageState::IN_USE)) return "using";
    return "unknown";
}

bool frameQualityReady(const SystemSnapshot& snap) {
    return snap.frame_quality.score >= 0.6f && !snap.frame_quality.guidance_needed;
}

bool gimbalReady(const SystemSnapshot& snap) {
    return !snap.preferences.gimbal_enabled ||
           (snap.device.gimbal_online && snap.device.gimbal_safe);
}

json sessionStateData(const SystemSnapshot& snap, bool gimbal_precheck_available = false) {
    const std::string owner = snap.session.precheck_owner.empty()
        ? "board"
        : snap.session.precheck_owner;
    return {
        {"active", snap.session.active},
        {"paused", snap.session.paused},
        {"session_id", snap.session.session_id},
        {"session_start_source", owner == "app" ? "app" : "board"},
        {"focus_state", publicFocusStateCode(snap.focus_state)},
        {"focus_reason", snap.focus_reason},
        {"risk_score", snap.behavior_context.risk_score},
        {"command_level", static_cast<int>(snap.reminder.level)},
        {"phone_signal", snap.behavior_context.phone_signal},
        {"phone_usage_state", phoneUsageStateToWire(snap.behavior_context.phone_usage_state)},
        {"phone_distraction_level", snap.behavior_context.phone_distraction_level},
        {"camera_ok", snap.device.camera_ok},
        {"frame_quality_ready", frameQualityReady(snap)},
        {"gimbal_ready", gimbalReady(snap)},
        {"elapsed_ms", snap.session.elapsed_ms},
        {"elapsed_sec", snap.session.elapsed_ms / 1000},
        {"remaining_ms", snap.session.remaining_ms},
        {"remaining_sec", snap.session.remaining_ms / 1000},
        {"target_duration_ms", snap.session.target_duration_ms},
        {"distraction_count", snap.session.distraction_count},
        {"reminder_count", snap.session.reminder_count},
        {"phone_distraction_count", snap.session.phone_distraction_count},
        {"calibration_completed", snap.session.calibration_completed},
        {"session_phase", sessionPhaseToString(deriveSessionPhase(snap.session))},
        {"precheck", precheckFlowData(snap, gimbal_precheck_available)},
        {"precheck_control", {
            {"owner", owner},
            {"app_can_skip_precheck", owner == "app"},
            {"board_can_continue", true}
        }},
        {"output_capability", outputCapabilityData(snap)},
        {"camera_preview", cameraPreviewData(snap)},
        {"behavior_context", behaviorContextData(snap)}
    };
}

json outputCapabilityData(const SystemSnapshot& snap) {
    return {
        {"board_screen_available", snap.device.board_screen_available},
        {"board_sound_available", snap.device.board_sound_available},
        {"screen_output_route", snap.preferences.screen_output_route},
        {"sound_output_route", snap.preferences.sound_output_route},
        {"band_available", snap.device.bracelet_connected},
        {"last_attempted_channels_mask", snap.device.last_attempted_channels_mask},
        {"last_delivered_channels_mask", snap.device.last_delivered_channels_mask},
        {"last_attempted_channels", notificationMaskToNames(snap.device.last_attempted_channels_mask)},
        {"last_delivered_channels", notificationMaskToNames(snap.device.last_delivered_channels_mask)}
    };
}

json reminderData(const SystemSnapshot& snap) {
    return {
        {"level", static_cast<int>(snap.reminder.level)},
        {"message", snap.reminder.message},
        {"since_ms", snap.reminder.since_ms}
    };
}

json precheckGuidanceData(const SystemSnapshot& snap) {
    return {
        {"distance_hint", snap.frame_quality.distance_hint},
        {"pose_hint", snap.frame_quality.pose_hint},
        {"ready", snap.frame_quality.precheck_ready},
        {"frame_quality_score", snap.frame_quality.score},
        {"frame_quality_reason", snap.frame_quality.reason},
        {"guidance_needed", snap.frame_quality.guidance_needed}
    };
}

json cameraPreviewData(const SystemSnapshot& snap) {
    return {
        {"camera_ok", snap.device.camera_ok},
        {"image_available", snap.camera_preview_image.available},
        {"image_sequence", snap.camera_preview_image.sequence},
        {"image_timestamp_ms", snap.camera_preview_image.timestamp_ms},
        {"image_width", snap.camera_preview_image.width},
        {"image_height", snap.camera_preview_image.height},
        {"image_pixel_format", snap.camera_preview_image.pixel_format},
        {"image_encoding", snap.camera_preview_image.encoding},
        {"image_interval_ms", 1200},
        {"quality_score", snap.frame_quality.score},
        {"quality_reason", snap.frame_quality.reason},
        {"guidance_needed", snap.frame_quality.guidance_needed},
        {"distance_hint", snap.frame_quality.distance_hint},
        {"pose_hint", snap.frame_quality.pose_hint},
        {"precheck_ready", snap.frame_quality.precheck_ready},
        {"debounce_ms", 1200}
    };
}

json behaviorContextData(const SystemSnapshot& snap) {
    return {
        {"risk_score", snap.behavior_context.risk_score},
        {"vision_signal", snap.behavior_context.vision_signal},
        {"phone_signal", snap.behavior_context.phone_signal},
        {"phone_distraction_level", snap.behavior_context.phone_distraction_level},
        {"phone_usage_state", snap.behavior_context.phone_usage_state},
        {"reason", snap.behavior_context.reason}
    };
}

json gimbalStatusData(const SystemSnapshot& snap) {
    return {
        {"connected", snap.device.gimbal_connected},
        {"online", snap.device.gimbal_online},
        {"safe", snap.device.gimbal_safe},
        {"xhome", snap.device.gimbal_xhome},
        {"yhome", snap.device.gimbal_yhome},
        {"calibrated", snap.device.gimbal_calibrated},
        {"boost", snap.device.gimbal_boost},
        {"motor", snap.device.gimbal_motor},
        {"en", snap.device.gimbal_en},
        {"ix", snap.device.gimbal_ix},
        {"iy", snap.device.gimbal_iy},
        {"angle_x", snap.device.gimbal_angle_x},
        {"angle_y", snap.device.gimbal_angle_y},
        {"last_seen_ms", snap.device.gimbal_last_seen_ms},
        {"last_error", snap.device.gimbal_last_error},
        {"initializing", snap.device.gimbal_initializing},
        {"auto_initialized", snap.device.gimbal_auto_initialized},
        {"last_command", snap.device.gimbal_last_command},
        {"last_command_status", snap.device.gimbal_last_command_status},
        {"last_command_response", snap.device.gimbal_last_command_response},
        {"enabled", snap.preferences.gimbal_enabled}
    };
}

bool gimbalPrecheckAvailable(const SystemSnapshot& snap, const GimbalMonitor* gimbal_monitor) {
    if (!gimbal_monitor) return false;
    if (!snap.preferences.gimbal_enabled) return false;
    return snap.device.gimbal_online && snap.device.gimbal_safe;
}

bool isGimbalMotionAction(const std::string& action) {
    return action == "auto_init" || action == "center_x" || action == "deg_x" ||
           action == "deg_y" || action == "set_xy" || action == "move_xy" ||
           action == "nudge_left" || action == "nudge_right" ||
           action == "nudge_up" || action == "nudge_down";
}

bool isGimbalDiagnosticAction(const std::string& action) {
    return action == "ver" || action == "ping" || action == "stat" ||
           action == "fault" || action == "angle";
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string notificationModeToString(NotificationMode mode) {
    return mode == NotificationMode::SILENT ? "silent" : "normal";
}

std::string powerProfileToString(PowerProfile profile) {
    return profile == PowerProfile::HOME ? "home" : "go";
}

NotificationMode notificationModeFromLegacySystemMode(int mode) {
    return mode == static_cast<int>(SystemMode::SILENT)
        ? NotificationMode::SILENT
        : NotificationMode::NORMAL;
}

PowerProfile powerProfileFromLegacySystemMode(int mode) {
    return mode == static_cast<int>(SystemMode::HOME)
        ? PowerProfile::HOME
        : PowerProfile::GO;
}

SystemMode systemModeFromNotificationMode(NotificationMode mode) {
    return mode == NotificationMode::SILENT ? SystemMode::SILENT : SystemMode::NORMAL;
}

NotificationMode parseNotificationModeOrDefault(const std::string& value,
                                                NotificationMode fallback) {
    const auto normalized = lowerCopy(value);
    if (normalized == "normal") return NotificationMode::NORMAL;
    if (normalized == "silent") return NotificationMode::SILENT;
    return fallback;
}

PowerProfile parsePowerProfileOrDefault(const std::string& value, PowerProfile fallback) {
    const auto normalized = lowerCopy(value);
    if (normalized == "home") return PowerProfile::HOME;
    if (normalized == "go") return PowerProfile::GO;
    return fallback;
}

int clampMinutes(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

int clampInt(int value, int min_value, int max_value) {
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

bool isValidSystemMode(int value) {
    return value == static_cast<int>(SystemMode::NORMAL) ||
           value == static_cast<int>(SystemMode::SILENT) ||
           value == static_cast<int>(SystemMode::HOME);
}

int parseSystemModeOrDefault(const std::string& value, int fallback) {
    const int parsed = parseIntOrDefault(value, fallback);
    return isValidSystemMode(parsed) ? parsed : fallback;
}

int sanitizeNotificationChannelMask(int value, bool allow_sound) {
    int mask = 0;
    if (value & kNotificationChannelScreenMask) mask |= kNotificationChannelScreenMask;
    if (value & kNotificationChannelBandMask) mask |= kNotificationChannelBandMask;
    if (allow_sound && (value & kNotificationChannelSoundMask)) {
        mask |= kNotificationChannelSoundMask;
    }
    // Legacy LIGHT input still maps to SCREEN.
    if (value & kNotificationChannelLightMask) mask |= kNotificationChannelScreenMask;
    return mask;
}

NotificationChannelMask parseChannelMaskFromValue(const json& value, bool allow_sound) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument("normal/silent channels mask must be integer");
    }
    const int raw = value.get<int>();
    return sanitizeNotificationChannelMask(raw, allow_sound);
}

NotificationChannelMask parseChannelMaskFromNames(const json& value, bool allow_sound) {
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
            if (allow_sound) {
                mask |= kNotificationChannelSoundMask;
            }
        } else if (name == "light") {
            // Legacy LIGHT input maps to SCREEN for backward compatibility.
            mask |= kNotificationChannelScreenMask;
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

float clampFloat(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

int clampFocusDurationMin(int value) {
    return clampMinutes(value, kMinFocusDurationMin, kMaxFocusDurationMin);
}

int clampedDefaultFocusDurationMin(const Preferences& prefs) {
    return clampFocusDurationMin(prefs.default_focus_duration_min);
}

Preferences sanitizePreferences(Preferences prefs) {
    prefs.away_timeout_min = clampMinutes(prefs.away_timeout_min, kMinAwayTimeoutMin, kMaxAwayTimeoutMin);
    prefs.default_focus_duration_min = clampFocusDurationMin(prefs.default_focus_duration_min);
    prefs.vibration_strength = clampFloat(prefs.vibration_strength, 0.0f, 1.0f);
    prefs.sound_volume = clampFloat(prefs.sound_volume, 0.0f, 1.0f);
    prefs.led_brightness = clampInt(prefs.led_brightness, 0, 100);
    prefs.reminder_sensitivity = clampFloat(prefs.reminder_sensitivity, 0.0f, 1.0f);
    return prefs;
}

int64_t minutesToMs(int minutes) {
    return static_cast<int64_t>(minutes) * 60000LL;
}

int durationMsToClampedMinutes(int64_t duration_ms, int fallback_min) {
    if (duration_ms <= 0) return clampFocusDurationMin(fallback_min);
    return clampFocusDurationMin(static_cast<int>((duration_ms + 59999LL) / 60000LL));
}

int targetDurationMinutesFromPayload(const json& data, const Preferences& prefs) {
    const int fallback_min = clampedDefaultFocusDurationMin(prefs);
    if (data.contains("target_duration_min")) {
        const int requested = data["target_duration_min"].get<int>();
        return requested > 0 ? clampFocusDurationMin(requested) : fallback_min;
    }
    if (data.contains("target_duration_ms")) {
        return durationMsToClampedMinutes(data["target_duration_ms"].get<int64_t>(), fallback_min);
    }
    if (data.contains("duration_ms")) {
        return durationMsToClampedMinutes(data["duration_ms"].get<int64_t>(), fallback_min);
    }
    return fallback_min;
}

bool isValidUploadedCounter(int value) {
    return value >= 0 && value <= kMaxUploadedCounter;
}

std::string normalizeOutputRoute(std::string value, const std::string& fallback) {
    value = lowerCopy(value);
    if (value == "board" || value == "phone") return value;
    return fallback;
}

json precheckFlowData(const SystemSnapshot& snap, bool gimbal_precheck_available) {
    json data = {
        {"session_phase", sessionPhaseToString(deriveSessionPhase(snap.session))},
        {"camera", {
            {"required", true},
            {"preview", cameraPreviewData(snap)},
            {"guidance", precheckGuidanceData(snap)}
        }}
    };
    if (gimbal_precheck_available) {
        data["gimbal"] = gimbalStatusData(snap);
    }
    return data;
}

json sessionReportData(const SessionReport& report,
                       int64_t target_duration_ms,
                       const std::string& end_reason) {
    const int64_t end_time_ms = wallClockMs();
    const int64_t start_time_ms =
        end_time_ms - std::max<int64_t>(0, report.total_duration * 1000);
    return {
        {"session_id", report.session_id},
        {"start_time_ms", start_time_ms},
        {"end_time_ms", end_time_ms},
        {"total_duration", report.total_duration},
        {"focus_duration", report.focus_duration},
        {"elapsed_ms", report.total_duration * 1000},
        {"target_duration_ms", target_duration_ms},
        {"remaining_ms", 0},
        {"distraction_count", report.distraction_count},
        {"reminder_count", report.reminder_count},
        {"phone_distraction_count", report.phone_distraction_count},
        {"end_reason", end_reason}
    };
}

int totalGimbalTimeoutMs(const std::vector<std::pair<std::string, int>>& commands) {
    int total = 0;
    for (const auto& item : commands) {
        total += item.second;
    }
    return total;
}

json gimbalResultData(const GimbalCommandResult& result) {
    return {
        {"success", result.success},
        {"timeout", result.timeout},
        {"command", result.command},
        {"start_response", result.start_response},
        {"final_response", result.final_response},
        {"error", result.error}
    };
}

} // namespace

AppCommandService::AppCommandService(SystemStateStore& store, SQLiteStore& sqlite_store,
                                     ReminderPolicy& reminder_policy, ModeManager& mode_mgr,
                                     GimbalMonitor* gimbal_monitor)
    : store_(store)
    , sqlite_store_(sqlite_store)
    , reminder_policy_(reminder_policy)
    , mode_mgr_(mode_mgr)
    , gimbal_monitor_(gimbal_monitor) {}

AppCommandService::~AppCommandService() {
    if (gimbal_task_thread_.joinable()) {
        gimbal_task_thread_.join();
    }
}

void AppCommandService::joinCompletedGimbalTaskLocked() {
    if (gimbal_task_thread_.joinable() && !gimbal_task_running_.load()) {
        gimbal_task_thread_.join();
    }
}

std::string AppCommandService::startGimbalTask(
    const std::string& action,
    std::vector<std::pair<std::string, int>> commands,
    int req_id) {
    if (commands.empty()) {
        return makeError(req_id, "Missing gimbal command");
    }

    const int estimated_timeout_ms = totalGimbalTimeoutMs(commands);
    std::string task_id;
    {
        std::lock_guard<std::mutex> lock(gimbal_task_mutex_);
        joinCompletedGimbalTaskLocked();
        if (gimbal_task_running_.load()) {
            return makeError(req_id, "Gimbal command already running");
        }

        gimbal_task_running_.store(true);
        task_id = "gimbal_" + std::to_string(wallClockMs()) + "_" +
                  std::to_string(++gimbal_task_seq_);
        gimbal_task_thread_ = std::thread(
            [this, task_id, action, commands = std::move(commands)]() mutable {
                bool success = true;
                bool timeout = false;
                std::string error;
                json steps = json::array();

                try {
                    for (const auto& item : commands) {
                        auto result = gimbal_monitor_->executeBusinessCommand(item.first, item.second);
                        steps.push_back(gimbalResultData(result));
                        if (!result.success) {
                            success = false;
                            timeout = result.timeout;
                            error = result.error;
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    success = false;
                    error = e.what();
                } catch (...) {
                    success = false;
                    error = "unknown_gimbal_error";
                }

                json push_data = {
                    {"task_id", task_id},
                    {"action", action},
                    {"success", success},
                    {"timeout", timeout},
                    {"state", success ? "finished" : "failed"},
                    {"error", error},
                    {"steps", steps}
                };
                if (!steps.empty()) {
                    push_data["command"] = steps.back().value("command", "");
                    push_data["start_response"] = steps.back().value("start_response", "");
                    push_data["final_response"] = steps.back().value("final_response", "");
                }
                gimbal_task_running_.store(false);
                store_.pushEvent(makePush("push_gimbal_result", push_data));
            });
    }

    return makeResponse(req_id, {
        {"accepted", true},
        {"task_id", task_id},
        {"action", action},
        {"state", "running"},
        {"estimated_timeout_ms", estimated_timeout_ms}
    });
}

void AppCommandService::loadPreferences() {
    Preferences prefs = store_.snapshot().preferences;
    prefs.system_mode = parseSystemModeOrDefault(
        sqlite_store_.getPreference("system_mode", std::to_string(prefs.system_mode)),
        prefs.system_mode);
    prefs.notification_mode = parseNotificationModeOrDefault(
        sqlite_store_.getPreference("notification_mode",
            notificationModeToString(notificationModeFromLegacySystemMode(prefs.system_mode))),
        notificationModeFromLegacySystemMode(prefs.system_mode));
    prefs.power_profile = parsePowerProfileOrDefault(
        sqlite_store_.getPreference("power_profile",
            powerProfileToString(powerProfileFromLegacySystemMode(prefs.system_mode))),
        powerProfileFromLegacySystemMode(prefs.system_mode));
    prefs.system_mode = static_cast<int>(systemModeFromNotificationMode(prefs.notification_mode));
    prefs.away_timeout_min = parseIntOrDefault(sqlite_store_.getPreference("away_timeout_min", std::to_string(prefs.away_timeout_min)),
                                               prefs.away_timeout_min);
    prefs.default_focus_duration_min = clampMinutes(
        parseIntOrDefault(sqlite_store_.getPreference("default_focus_duration_min",
                                                      std::to_string(prefs.default_focus_duration_min)),
                          prefs.default_focus_duration_min),
        5, 120);
    prefs.vibration_strength = parseFloatOrDefault(sqlite_store_.getPreference("vibration_strength", std::to_string(prefs.vibration_strength)),
                                                   prefs.vibration_strength);
    prefs.sound_volume = clampFloat(
        parseFloatOrDefault(sqlite_store_.getPreference("sound_volume", std::to_string(prefs.sound_volume)),
                            prefs.sound_volume),
        0.0f, 1.0f);
    prefs.led_brightness = parseIntOrDefault(sqlite_store_.getPreference("led_brightness", std::to_string(prefs.led_brightness)),
                                             prefs.led_brightness);
    prefs.reminder_sensitivity = parseFloatOrDefault(sqlite_store_.getPreference("reminder_sensitivity", std::to_string(prefs.reminder_sensitivity)),
                                                     prefs.reminder_sensitivity);
    prefs.normal_channels_mask = sanitizeNotificationChannelMask(
        parseNotificationMaskOrDefault(
            sqlite_store_.getPreference("normal_channels_mask", std::to_string(prefs.normal_channels_mask)),
            prefs.normal_channels_mask), true);
    prefs.silent_channels_mask = sanitizeNotificationChannelMask(
        parseNotificationMaskOrDefault(
            sqlite_store_.getPreference("silent_channels_mask", std::to_string(prefs.silent_channels_mask)),
            prefs.silent_channels_mask), false);
    const auto legacy_screen_route = normalizeOutputRoute(
        sqlite_store_.getPreference("screen_output_route", prefs.screen_output_route),
        prefs.screen_output_route);
    const auto legacy_sound_route = normalizeOutputRoute(
        sqlite_store_.getPreference("sound_output_route", prefs.sound_output_route),
        prefs.sound_output_route);
    prefs.screen_output_route = legacy_screen_route;
    prefs.sound_output_route = legacy_sound_route;
    prefs.gimbal_enabled = parseIntOrDefault(
        sqlite_store_.getPreference("gimbal_enabled", prefs.gimbal_enabled ? "1" : "0"),
        prefs.gimbal_enabled ? 1 : 0) != 0;
    prefs = sanitizePreferences(prefs);
    if (const char* disabled = std::getenv("EDGEFOCUS_DISABLE_LCD")) {
        const auto value = lowerCopy(disabled);
        if (value == "1" || value == "true" || value == "yes" || value == "on") {
            prefs.screen_output_route = "phone";
        }
    }
    if (const char* disabled = std::getenv("EDGEFOCUS_DISABLE_SOUND")) {
        const auto value = lowerCopy(disabled);
        if (value == "1" || value == "true" || value == "yes" || value == "on") {
            prefs.sound_output_route = "phone";
        }
    }
    if (const char* disabled = std::getenv("EDGEFOCUS_DISABLE_GIMBAL")) {
        const auto value = lowerCopy(disabled);
        if (value == "1" || value == "true" || value == "yes" || value == "on") {
            prefs.gimbal_enabled = false;
        }
    }
    prefs = sanitizePreferences(prefs);
    store_.updatePreferences(prefs);
    mode_mgr_.setMode(systemModeFromNotificationMode(prefs.notification_mode));

    DeviceStatus device = store_.snapshot().device;
    device.system_mode = systemModeFromNotificationMode(prefs.notification_mode);
    device.notification_mode = prefs.notification_mode;
    device.power_profile = prefs.power_profile;
    store_.updateDeviceStatus(device);

    SessionInfo session = store_.snapshot().session;
    session.away_timeout_ms = minutesToMs(prefs.away_timeout_min);
    if (session.active) {
        store_.updateSessionIfCurrent(session);
    }
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
        if (cmd == "upload_local_session")   return handleUploadLocalSession(data, req_id);
        if (cmd == "get_console_data")       return handleGetConsoleData(req_id);
        if (cmd == "session_feedback")       return handleSessionFeedback(data, req_id);
        if (cmd == "get_session_history")    return handleGetSessionHistory(data, req_id);
        if (cmd == "get_preferences")        return handleGetPreferences(req_id);
        if (cmd == "get_gimbal_status")      return handleGetGimbalStatus(req_id);
        if (cmd == "gimbal_control")         return handleGimbalControl(data, req_id);
        if (cmd == "get_camera_preview_image") return handleGetCameraPreviewImage(req_id);
    } catch (const json::exception& e) {
        return makeError(req_id, std::string("Invalid command data: ") + e.what());
    } catch (const std::exception& e) {
        return makeError(req_id, std::string("Command failed: ") + e.what());
    }

    return makeError(req_id, "Unknown command: " + cmd);
}

std::string AppCommandService::handleGetDeviceStatus(int req_id) {
    auto snap = store_.snapshot();
    json battery_percent = snap.device.battery_percent >= 0
        ? json(snap.device.battery_percent)
        : json(nullptr);
    json battery_voltage_mv = snap.device.battery_voltage_mv >= 0
        ? json(snap.device.battery_voltage_mv)
        : json(nullptr);
    json data = {
        {"battery_available", snap.device.battery_percent >= 0 || snap.device.battery_voltage_mv >= 0},
        {"battery_percent", battery_percent},
        {"battery_voltage_mv", battery_voltage_mv},
        {"battery_charging", snap.device.battery_charging},
        {"power_source", snap.device.power_source},
        {"system_mode", static_cast<int>(snap.device.system_mode)},
        {"notification_mode", notificationModeToString(snap.device.notification_mode)},
        {"power_profile", powerProfileToString(snap.device.power_profile)},
        {"camera_ok", snap.device.camera_ok},
        {"bracelet_connected", snap.device.bracelet_connected},
        {"board_screen_available", snap.device.board_screen_available},
        {"board_sound_available", snap.device.board_sound_available},
        {"output_capability", outputCapabilityData(snap)},
        {"gimbal_connected", snap.device.gimbal_connected},
        {"gimbal_online", snap.device.gimbal_online},
        {"gimbal", gimbalStatusData(snap)},
        {"is_session_active", snap.session.active}
    };
    return makeResponse(req_id, data);
}

std::string AppCommandService::handleGetCurrentState(int req_id) {
    auto snap = store_.snapshot();
    const bool gimbal_precheck_available = gimbalPrecheckAvailable(snap, gimbal_monitor_);
    json data = sessionStateData(snap, gimbal_precheck_available);
    data["reminder"] = reminderData(snap);
    data["precheck_guidance"] = precheckGuidanceData(snap);
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
        {"start_time_ms", last.start_time_ms},
        {"end_time_ms", last.end_time_ms},
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
        int target_min = targetDurationMinutesFromPayload(data, snap.preferences);
        SessionController controller(store_, sqlite_store_);
        auto started = controller.startSession(target_min, "app");
        const auto& s = started.session;
        const auto started_snapshot = store_.snapshot();
        LOGI("AppCommandService", "Session started: %s", s.session_id.c_str());
        json resp = sessionStateData(
            started_snapshot,
            gimbalPrecheckAvailable(started_snapshot, gimbal_monitor_));
        resp["success"] = started.success;
        resp["already_active"] = started.already_active;
        return makeResponse(req_id, resp);

    } else if (action == "skip_precheck" || action == "skip_calibration") {
        if (!snap.session.active) {
            json resp = {{"success", false}, {"message", "No active session"}};
            return makeResponse(req_id, resp);
        }
        const std::string owner = snap.session.precheck_owner.empty()
            ? "board"
            : snap.session.precheck_owner;
        if (owner != "app") {
            json resp = sessionStateData(snap, gimbalPrecheckAvailable(snap, gimbal_monitor_));
            resp["success"] = false;
            resp["message"] = "App is not allowed to skip board-owned precheck";
            return makeResponse(req_id, resp);
        }
        SessionController controller(store_, sqlite_store_);
        bool ok = controller.skipPrecheck("app_skip_precheck");
        auto after = store_.snapshot();
        const bool gimbal_precheck_available = gimbalPrecheckAvailable(after, gimbal_monitor_);
        json resp = sessionStateData(after, gimbal_precheck_available);
        resp["success"] = ok;
        store_.pushEvent(makePush("push_state", sessionStateData(after, gimbal_precheck_available)));
        return makeResponse(req_id, resp);

    } else if (action == "pause") {
        if (!snap.session.active || snap.session.paused) {
            json resp = {{"success", false}, {"message", "No active session to pause"}};
            return makeResponse(req_id, resp);
        }
        SessionController controller(store_, sqlite_store_);
        bool ok = controller.pauseSession("User paused");
        auto after = store_.snapshot();
        const bool gimbal_precheck_available = gimbalPrecheckAvailable(after, gimbal_monitor_);
        json resp = sessionStateData(after, gimbal_precheck_available);
        resp["success"] = ok;
        store_.pushEvent(makePush("push_state", sessionStateData(after, gimbal_precheck_available)));
        return makeResponse(req_id, resp);

    } else if (action == "resume") {
        if (!snap.session.active || !snap.session.paused) {
            json resp = {{"success", false}, {"message", "No paused session to resume"}};
            return makeResponse(req_id, resp);
        }
        SessionController controller(store_, sqlite_store_);
        bool ok = controller.resumeSession("User resumed");
        auto after = store_.snapshot();
        const bool gimbal_precheck_available = gimbalPrecheckAvailable(after, gimbal_monitor_);
        json resp = sessionStateData(after, gimbal_precheck_available);
        resp["success"] = ok;
        store_.pushEvent(makePush("push_state", sessionStateData(after, gimbal_precheck_available)));
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
            json push_data = sessionReportData(report, target_duration_ms, "user_ended");
            store_.pushEvent(makePush("push_session_end", push_data));
        }

        json resp = sessionReportData(report, target_duration_ms, "user_ended");
        resp["success"] = ended.success;
        return makeResponse(req_id, resp);
    }

    return makeError(req_id, "Unknown action: " + action);
}

std::string AppCommandService::handleUpdatePreferences(const json& data, int req_id) {
    Preferences prefs = store_.snapshot().preferences;

    if (data.contains("system_mode")) {
        int mode = data["system_mode"].get<int>();
        if (!isValidSystemMode(mode)) {
            throw std::invalid_argument("invalid system_mode");
        }
        prefs.notification_mode = notificationModeFromLegacySystemMode(mode);
        prefs.power_profile = powerProfileFromLegacySystemMode(mode);
    }
    if (data.contains("notification_mode")) {
        if (!data["notification_mode"].is_string()) {
            throw std::invalid_argument("notification_mode must be string");
        }
        const auto parsed = parseNotificationModeOrDefault(
            data["notification_mode"].get<std::string>(),
            prefs.notification_mode);
        prefs.notification_mode = parsed;
    }
    if (data.contains("power_profile")) {
        if (!data["power_profile"].is_string()) {
            throw std::invalid_argument("power_profile must be string");
        }
        prefs.power_profile = parsePowerProfileOrDefault(
            data["power_profile"].get<std::string>(),
            prefs.power_profile);
    }
    prefs.system_mode = static_cast<int>(systemModeFromNotificationMode(prefs.notification_mode));
    mode_mgr_.setMode(systemModeFromNotificationMode(prefs.notification_mode));
    DeviceStatus ds = store_.snapshot().device;
    ds.system_mode = systemModeFromNotificationMode(prefs.notification_mode);
    ds.notification_mode = prefs.notification_mode;
    ds.power_profile = prefs.power_profile;
    store_.updateDeviceStatus(ds);
    if (data.contains("away_timeout_min")) {
        prefs.away_timeout_min = clampMinutes(
            data["away_timeout_min"].get<int>(),
            kMinAwayTimeoutMin,
            kMaxAwayTimeoutMin);
        SessionInfo s = store_.snapshot().session;
        s.away_timeout_ms = minutesToMs(prefs.away_timeout_min);
        if (s.active) {
            store_.updateSessionIfCurrent(s);
        }
    }
    if (data.contains("default_focus_duration_min")) {
        prefs.default_focus_duration_min = clampFocusDurationMin(data["default_focus_duration_min"].get<int>());
    }
    if (data.contains("vibration_strength")) {
        prefs.vibration_strength = clampFloat(data["vibration_strength"].get<float>(), 0.0f, 1.0f);
    }
    if (data.contains("sound_volume")) {
        prefs.sound_volume = clampFloat(data["sound_volume"].get<float>(), 0.0f, 1.0f);
    }
    if (data.contains("led_brightness")) {
        prefs.led_brightness = clampInt(data["led_brightness"].get<int>(), 0, 100);
    }
    if (data.contains("reminder_sensitivity")) {
        prefs.reminder_sensitivity = clampFloat(data["reminder_sensitivity"].get<float>(), 0.0f, 1.0f);
    }
    if (data.contains("normal_channels_mask")) {
        prefs.normal_channels_mask = parseChannelMaskFromValue(data["normal_channels_mask"], true);
    } else if (data.contains("normal_channels")) {
        prefs.normal_channels_mask = parseChannelMaskFromNames(data["normal_channels"], true);
    }
    if (data.contains("silent_channels_mask")) {
        prefs.silent_channels_mask = parseChannelMaskFromValue(data["silent_channels_mask"], false);
    } else if (data.contains("silent_channels")) {
        prefs.silent_channels_mask = parseChannelMaskFromNames(data["silent_channels"], false);
    }
    if (data.contains("screen_output_route")) {
        prefs.screen_output_route = normalizeOutputRoute(data["screen_output_route"].get<std::string>(),
                                                         prefs.screen_output_route);
    }
    if (data.contains("sound_output_route")) {
        prefs.sound_output_route = normalizeOutputRoute(data["sound_output_route"].get<std::string>(),
                                                        prefs.sound_output_route);
    }
    if (data.contains("gimbal_enabled")) {
        prefs.gimbal_enabled = data["gimbal_enabled"].get<bool>();
    }
    prefs = sanitizePreferences(prefs);

    store_.updatePreferences(prefs);

    sqlite_store_.savePreference("system_mode", std::to_string(prefs.system_mode));
    sqlite_store_.savePreference("notification_mode", notificationModeToString(prefs.notification_mode));
    sqlite_store_.savePreference("power_profile", powerProfileToString(prefs.power_profile));
    sqlite_store_.savePreference("away_timeout_min", std::to_string(prefs.away_timeout_min));
    sqlite_store_.savePreference("default_focus_duration_min", std::to_string(prefs.default_focus_duration_min));
    sqlite_store_.savePreference("vibration_strength", std::to_string(prefs.vibration_strength));
    sqlite_store_.savePreference("sound_volume", std::to_string(prefs.sound_volume));
    sqlite_store_.savePreference("led_brightness", std::to_string(prefs.led_brightness));
    sqlite_store_.savePreference("reminder_sensitivity", std::to_string(prefs.reminder_sensitivity));
    sqlite_store_.savePreference("normal_channels_mask", std::to_string(prefs.normal_channels_mask));
    sqlite_store_.savePreference("silent_channels_mask", std::to_string(prefs.silent_channels_mask));
    sqlite_store_.savePreference("screen_output_route", prefs.screen_output_route);
    sqlite_store_.savePreference("sound_output_route", prefs.sound_output_route);
    sqlite_store_.savePreference("gimbal_enabled", prefs.gimbal_enabled ? "1" : "0");

    json resp = {
        {"system_mode", prefs.system_mode},
        {"notification_mode", notificationModeToString(prefs.notification_mode)},
        {"power_profile", powerProfileToString(prefs.power_profile)},
        {"away_timeout_min", prefs.away_timeout_min},
        {"default_focus_duration_min", prefs.default_focus_duration_min},
        {"vibration_strength", prefs.vibration_strength},
        {"sound_volume", prefs.sound_volume},
        {"led_brightness", prefs.led_brightness},
        {"reminder_sensitivity", prefs.reminder_sensitivity},
        {"normal_channels_mask", prefs.normal_channels_mask},
        {"silent_channels_mask", prefs.silent_channels_mask},
        {"normal_channels", notificationMaskToNames(prefs.normal_channels_mask)},
        {"silent_channels", notificationMaskToNames(prefs.silent_channels_mask)},
        {"screen_output_route", prefs.screen_output_route},
        {"sound_output_route", prefs.sound_output_route},
        {"gimbal_enabled", prefs.gimbal_enabled}
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
    auto parsed = parsePhoneSyncPayload(data);

    auto snap = store_.snapshot();

    if (!parsed.active) {
        PhoneUsageSignal sig;
        sig.active = false;
        sig.distraction_level = parsed.distraction_level;
        sig.usage_state = parsed.usage_state;
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
    sig.distraction_level = parsed.distraction_level;
    sig.usage_state = parsed.usage_state;
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

std::string AppCommandService::handleUploadLocalSession(const json& data, int req_id) {
    const std::string session_id = data.value("session_id", "");
    if (session_id.empty()) {
        return makeError(req_id, "Missing session_id");
    }

    const bool already_exists = sqlite_store_.sessionExists(session_id);
    if (!already_exists) {
        const int64_t start_time_ms = data.value("start_time_ms", 0LL);
        const int64_t end_time_ms = data.value("end_time_ms", 0LL);
        const int64_t total_duration = data.value("total_duration", 0LL);
        const int64_t focus_duration = data.value("focus_duration", total_duration);
        const int distraction_count = data.value("distraction_count", 0);
        const int reminder_count = data.value("reminder_count", 0);
        const int phone_distraction_count = data.value("phone_distraction_count", 0);

        if (start_time_ms <= 0) {
            return makeError(req_id, "Invalid start_time_ms");
        }
        if (end_time_ms < start_time_ms) {
            return makeError(req_id, "Invalid end_time_ms");
        }
        if (total_duration <= 0 || total_duration > kMaxLocalSessionDurationSec) {
            return makeError(req_id, "Invalid total_duration");
        }
        if (focus_duration < 0 || focus_duration > total_duration) {
            return makeError(req_id, "Invalid focus_duration");
        }
        if (!isValidUploadedCounter(distraction_count) ||
            !isValidUploadedCounter(reminder_count) ||
            !isValidUploadedCounter(phone_distraction_count)) {
            return makeError(req_id, "Invalid count field");
        }

        if (!sqlite_store_.saveSession(session_id, start_time_ms, total_duration * 1000)) {
            return makeError(req_id, "Failed to save local session");
        }
        if (!sqlite_store_.updateSessionEnd(session_id, end_time_ms, total_duration,
                                            focus_duration, distraction_count,
                                            reminder_count, phone_distraction_count,
                                            "app_local_upload")) {
            return makeError(req_id, "Failed to finalize local session");
        }

        auto snap = store_.snapshot();
        TodayStats today = snap.today;
        today.total_seconds += total_duration;
        today.focus_seconds += focus_duration;
        today.sessions++;
        today.events += distraction_count;
        today.phone_distraction_count += phone_distraction_count;
        store_.updateTodayStats(today);
    }

    json resp = {
        {"stored", true},
        {"already_exists", already_exists},
        {"session_id", session_id}
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
            {"battery_voltage_mv", snap.device.battery_voltage_mv},
            {"battery_charging", snap.device.battery_charging},
            {"power_source", snap.device.power_source},
            {"system_mode", static_cast<int>(snap.device.system_mode)},
            {"camera_ok", snap.device.camera_ok},
            {"bracelet_connected", snap.device.bracelet_connected},
            {"band_last_alert_level", snap.device.band_last_alert_level},
            {"band_last_alert_pattern", snap.device.band_last_alert_pattern},
            {"band_write_attempt_count", snap.device.band_write_attempt_count},
            {"band_last_write_status", snap.device.band_last_write_status},
            {"gimbal", gimbalStatusData(snap)}
        }},
        {"reminder", {
            {"level", static_cast<int>(snap.reminder.level)},
            {"message", snap.reminder.message},
            {"since_ms", snap.reminder.since_ms}
        }},
        {"output_capability", outputCapabilityData(snap)},
        {"frame_quality", {
            {"score", snap.frame_quality.score},
            {"reason", snap.frame_quality.reason},
            {"guidance_needed", snap.frame_quality.guidance_needed},
            {"distance_hint", snap.frame_quality.distance_hint},
            {"pose_hint", snap.frame_quality.pose_hint},
            {"precheck_ready", snap.frame_quality.precheck_ready}
        }},
        {"camera_preview", cameraPreviewData(snap)},
        {"behavior_context", behaviorContextData(snap)},
        {"focus_reason", snap.focus_reason},
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
    int limit = clampInt(data.value("limit", 1), 1, kMaxSessionHistoryLimit);

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
        {"notification_mode", notificationModeToString(snap.preferences.notification_mode)},
        {"power_profile", powerProfileToString(snap.preferences.power_profile)},
        {"away_timeout_min", snap.preferences.away_timeout_min},
        {"default_focus_duration_min", snap.preferences.default_focus_duration_min},
        {"vibration_strength", snap.preferences.vibration_strength},
        {"sound_volume", snap.preferences.sound_volume},
        {"led_brightness", snap.preferences.led_brightness},
        {"reminder_sensitivity", snap.preferences.reminder_sensitivity},
        {"normal_channels_mask", snap.preferences.normal_channels_mask},
        {"silent_channels_mask", snap.preferences.silent_channels_mask},
        {"normal_channels", notificationMaskToNames(snap.preferences.normal_channels_mask)},
        {"silent_channels", notificationMaskToNames(snap.preferences.silent_channels_mask)},
        {"screen_output_route", snap.preferences.screen_output_route},
        {"sound_output_route", snap.preferences.sound_output_route},
        {"gimbal_enabled", snap.preferences.gimbal_enabled}
    };
    return makeResponse(req_id, resp);
}

std::string AppCommandService::handleGetGimbalStatus(int req_id) {
    auto snap = store_.snapshot();
    return makeResponse(req_id, gimbalStatusData(snap));
}

std::string AppCommandService::handleGimbalControl(const json& data, int req_id) {
    if (!store_.snapshot().preferences.gimbal_enabled) {
        return makeError(req_id, "Gimbal disabled");
    }
    if (!gimbal_monitor_) {
        return makeError(req_id, "Gimbal control unavailable");
    }

    const std::string action = lowerCopy(data.value("action", ""));
    if (isGimbalDiagnosticAction(action) && !data.value("debug", false)) {
        return makeError(req_id, "Gimbal diagnostic action requires debug=true");
    }
    if (isGimbalMotionAction(action)) {
        const auto snap = store_.snapshot();
        if (!snap.device.gimbal_online || !snap.device.gimbal_safe) {
            return makeError(req_id, "Gimbal not ready for motion");
        }
    }
    if (action == "auto_init") {
        return startGimbalTask(action, {{"CENTER X", 30000}, {"DEG Y 0", 10000}}, req_id);
    }

    std::string command;
    int timeout_ms = 500;
    if (action == "stop") {
        command = "STOP";
    } else if (action == "ver") {
        command = "VER";
    } else if (action == "ping") {
        command = "PING";
    } else if (action == "stat") {
        command = "STAT";
    } else if (action == "fault") {
        command = "FAULT";
    } else if (action == "angle") {
        command = "ANGLE";
    } else if (action == "center_x") {
        command = "CENTER X";
        timeout_ms = 30000;
    } else if (action == "deg_x") {
        int x = data.value("x_deg", -1);
        if (x < 0 || x > 180) return makeError(req_id, "Invalid x_deg");
        command = "DEG X " + std::to_string(x);
        timeout_ms = 30000;
    } else if (action == "deg_y") {
        int y = data.value("y_deg", -1);
        if (y < 0 || y > 359) return makeError(req_id, "Invalid y_deg");
        command = "DEG Y " + std::to_string(y);
        timeout_ms = 10000;
    } else if (action == "set_xy") {
        int x = data.value("x_deg", -1);
        int y = data.value("y_deg", -1);
        if (x < 0 || x > 180 || y < 0 || y > 359) return makeError(req_id, "Invalid xy target");
        command = "SET XY " + std::to_string(x) + " " + std::to_string(y);
        timeout_ms = 45000;
    } else if (action == "move_xy") {
        int x = data.value("x_deg", -1);
        int y = data.value("y_deg", -1);
        int speed = data.value("speed", -1);
        if (x < 0 || x > 180 || y < 0 || y > 359 || speed < 1 || speed > 90) {
            return makeError(req_id, "Invalid xy move");
        }
        command = "MOVE XY " + std::to_string(x) + " " + std::to_string(y) + " " +
                  std::to_string(speed);
        timeout_ms = 60000;
    } else if (action == "nudge_left" || action == "nudge_right" ||
               action == "nudge_up" || action == "nudge_down") {
        const int step = clampInt(data.value("step_deg", 3), 1, 8);
        auto snap = store_.snapshot();
        int x = clampInt(static_cast<int>(std::lround(snap.device.gimbal_angle_x)), 0, 180);
        int y = clampInt(static_cast<int>(std::lround(snap.device.gimbal_angle_y)), 0, 359);
        if (action == "nudge_left") {
            y = clampInt(y - step, 0, 359);
        } else if (action == "nudge_right") {
            y = clampInt(y + step, 0, 359);
        } else if (action == "nudge_up") {
            x = clampInt(x - step, 0, 180);
        } else {
            x = clampInt(x + step, 0, 180);
        }
        command = "SET XY " + std::to_string(x) + " " + std::to_string(y);
        timeout_ms = 15000;
    } else {
        return makeError(req_id, "Unknown gimbal action: " + action);
    }

    if (isGimbalMotionAction(action)) {
        return startGimbalTask(action, {{command, timeout_ms}}, req_id);
    }

    auto result = gimbal_monitor_->executeBusinessCommand(command, timeout_ms);
    return makeResponse(req_id, gimbalResultData(result));
}

std::string AppCommandService::handleGetCameraPreviewImage(int req_id) {
    auto snap = store_.snapshot();
    const auto& image = snap.camera_preview_image;
    json resp = {
        {"available", image.available},
        {"timestamp_ms", image.timestamp_ms},
        {"sequence", image.sequence},
        {"width", image.width},
        {"height", image.height},
        {"pixel_format", image.pixel_format},
        {"encoding", image.encoding},
        {"source", image.source},
        {"data_base64", image.data_base64},
        {"quality", cameraPreviewData(snap)}
    };
    return makeResponse(req_id, resp);
}

} // namespace edgefocus
