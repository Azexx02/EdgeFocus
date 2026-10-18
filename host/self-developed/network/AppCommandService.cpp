#include "AppCommandService.h"
#include "network/AppEventOutbox.h"

#include "app/Logger.h"
#include "app/SessionController.h"
#include "app/SessionPhase.h"
#include "behavior/EventDetector.h"
#include "behavior/PhoneFactBuffer.h"
#include "network/PhoneSyncParser.h"
#include "session/SessionOrchestrator.h"
#include "session/SessionSnapshotPublisher.h"
#include "device/GimbalMonitor.h"
#include "device/GimbalPrecheckController.h"
#include "device/DeviceManager.h"
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
bool gimbalPrecheckAvailable(
    const SystemSnapshot& snap,
    const gimbal::GimbalPrecheckController* gimbal_precheck);

const char* applicabilityWire(session::Applicability value) {
    switch (value) {
        case session::Applicability::Active: return "ACTIVE";
        case session::Applicability::NotPresent: return "NOT_PRESENT";
        case session::Applicability::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* sourceModeWire(session::SourceMode value) {
    switch (value) {
        case session::SourceMode::LiveCamera: return "LIVE_CAMERA";
        case session::SourceMode::RecordedInput: return "RECORDED_INPUT";
        case session::SourceMode::EvidenceReplay: return "EVIDENCE_REPLAY";
    }
    return "LIVE_CAMERA";
}

json phoneCategoryWire(const std::optional<session::PhoneCategory>& value) {
    if (!value) return nullptr;
    switch (*value) {
        case session::PhoneCategory::Learning: return "LEARNING";
        case session::PhoneCategory::Entertainment: return "ENTERTAINMENT";
        case session::PhoneCategory::GeneralOrUnknown: return "GENERAL_OR_UNKNOWN";
    }
    return nullptr;
}

json precheckGimbalControls(const session::SessionSnapshot& value) {
    const bool precheck = value.phase == session::Phase::Precheck &&
                          value.runtime_active;
    const bool healthy = value.gimbal.available && !value.gimbal.health.degraded;
    const bool manual = precheck && healthy &&
                        value.gimbal.state == "manual_available";
    const bool skip = precheck && healthy &&
        !value.gimbal.search_completed &&
        (value.gimbal.state == "searching" ||
         value.gimbal.state == "centering" ||
         value.gimbal.state == "framing" ||
         value.gimbal.state == "holding");
    return {
        {"manual_enabled", manual},
        {"skip_search_enabled", skip}
    };
}

json gimbalPrecheckPushData(const session::SessionSnapshot& value) {
    return {
        {"session_id", value.context.session_id},
        {"state", value.gimbal.state},
        {"degraded", value.gimbal.health.degraded},
        {"health_status", value.gimbal.health.status},
        {"detail", value.gimbal.detail},
        {"fault_class", value.gimbal.fault_class},
        {"fault_code", value.gimbal.fault_code},
        {"search_completed", value.gimbal.search_completed},
        {"controls", precheckGimbalControls(value)}
    };
}

json stableSessionSnapshotData(const session::SessionSnapshot& value) {
    json level = value.assessment.level ? json(*value.assessment.level) : json(nullptr);
    const auto gimbal_controls = precheckGimbalControls(value);
    return {
        {"schema_version", value.schema_version},
        {"sequence", value.sequence},
        {"snapshot_ms", value.snapshot_ms},
        {"session", {
            {"id", value.context.session_id},
            {"phase", session::phase_name(value.phase)},
            {"active", value.formal_active},
            {"runtime_active", value.runtime_active},
            {"elapsed_ms", value.elapsed_ms},
            {"remaining_ms", value.remaining_ms},
            {"preparation_state", session::preparation_state_name(value.preparation.state)},
            {"preparation_error", value.preparation.error.empty()
                ? json(nullptr) : json(value.preparation.error)},
            {"preparation_failed_at_ms", value.preparation.failed_at_ms},
            {"start_generation", value.preparation.generation},
            {"operation_id", value.preparation.operation_id.empty()
                ? json(nullptr) : json(value.preparation.operation_id)}
        }},
        {"source", {{"mode", sourceModeWire(value.context.source_mode)}}},
        {"visual", {
            {"presence", value.visual.presence ? json(*value.visual.presence) : json(nullptr)},
            {"prone", value.visual.prone ? json(*value.visual.prone) : json(nullptr)},
            {"evidence_timestamp_ms", value.visual.evidence_timestamp_ms},
            {"frame_id", value.visual.frame_id},
            {"inference_fps", value.visual.inference_fps},
            {"inference_latency_ms", value.visual.inference_latency_ms}
        }},
        {"phone", {
            {"fresh", value.phone.fresh},
            {"active", value.phone.active},
            {"category", phoneCategoryWire(value.phone.category)},
            {"sequence", value.phone.sequence}
        }},
        {"assessment", {
            {"applicability", applicabilityWire(value.assessment.applicability)},
            {"level", level},
            {"reasons", value.assessment.reasons},
            {"would_remind", value.assessment.would_remind}
        }},
        {"gimbal", {
            {"state", value.gimbal.state},
            {"protocol_version", value.gimbal.protocol_version},
            {"capabilities", value.gimbal.capabilities},
            {"available", value.gimbal.available},
            {"degraded_reason", value.gimbal.degraded_reason},
            {"link_health", value.gimbal.link_health},
            {"control_mode", value.gimbal.control_mode},
            {"degraded", value.gimbal.health.degraded},
            {"health_status", value.gimbal.health.status},
            {"angle_x", value.gimbal.angle_x ? json(*value.gimbal.angle_x) : json(nullptr)},
            {"angle_y", value.gimbal.angle_y ? json(*value.gimbal.angle_y) : json(nullptr)},
            {"current_yaw10", value.gimbal.current_yaw10 ? json(*value.gimbal.current_yaw10) : json(nullptr)},
            {"current_pitch10", value.gimbal.current_pitch10 ? json(*value.gimbal.current_pitch10) : json(nullptr)},
            {"target_yaw10", value.gimbal.target_yaw10 ? json(*value.gimbal.target_yaw10) : json(nullptr)},
            {"target_pitch10", value.gimbal.target_pitch10 ? json(*value.gimbal.target_pitch10) : json(nullptr)},
            {"motion_generation", value.gimbal.motion_generation ? json(*value.gimbal.motion_generation) : json(nullptr)},
            {"settled", value.gimbal.settled},
            {"manual_lease", value.gimbal.manual_lease},
            {"lease_owner_id", value.gimbal.lease_owner_id},
            {"lease_epoch", value.gimbal.lease_epoch},
            {"lease_expires_at", value.gimbal.lease_expires_at_ms},
            {"search_generation", value.gimbal.search_generation},
            {"waypoint_index", value.gimbal.waypoint_index},
            {"subject_found", value.gimbal.subject_found},
            {"search_completed", value.gimbal.search_completed},
            {"calibration_valid", value.gimbal.calibration_valid},
            {"calibration_epoch", value.gimbal.calibration_epoch},
            {"origin_valid", value.gimbal.origin_valid},
            {"origin_epoch", value.gimbal.origin_epoch},
            {"recoverable", value.gimbal.recoverable},
            {"fault_class", value.gimbal.fault_class},
            {"fault_code", value.gimbal.fault_code},
            {"fault_latched_reason", value.gimbal.fault_latched_reason},
            {"last_pose_age_ms", value.gimbal.last_pose_age_ms},
            {"last_queued_sequence", value.gimbal.last_queued_sequence},
            {"acked_through_sequence", value.gimbal.acked_through_sequence},
            {"current_protection_state", value.gimbal.current_protection_state},
            {"detail", value.gimbal.detail},
            {"controls", gimbal_controls}
        }},
        {"degraded_features", value.degraded_features}
    };
}

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

const char* powerProfileWire(PowerProfile profile) {
    return profile == PowerProfile::HOME ? "home" : "go";
}

json externalPowerPresentWire(const DeviceStatus& device) {
    return device.external_power_present
        ? json(*device.external_power_present)
        : json(nullptr);
}

json externalPowerSampleAgeWire(const DeviceStatus& device) {
    return device.external_power_sample_age_ms >= 0
        ? json(device.external_power_sample_age_ms)
        : json(nullptr);
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
        {"external_power_state", snap.device.external_power_state},
        {"external_power_present", externalPowerPresentWire(snap.device)},
        {"external_power_sample_age_ms", externalPowerSampleAgeWire(snap.device)},
        {"power_profile", powerProfileWire(snap.device.power_profile)},
        {"power_profile_source", snap.device.power_profile_source},
        {"manual_override_active", snap.device.manual_override_active},
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

bool gimbalPrecheckAvailable(
    const SystemSnapshot& snap,
    const gimbal::GimbalPrecheckController* gimbal_precheck) {
    if (!gimbal_precheck) return false;
    if (!snap.preferences.gimbal_enabled) return false;
    return true;
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
    prefs.sound_volume = clampFloat(prefs.sound_volume, 0.0f, 1.0f);
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

struct AppCommandService::GimbalPushState {
    std::mutex mutex;
    std::string signature;
};

AppCommandService::AppCommandService(SystemStateStore& store, SQLiteStore& sqlite_store,
                                     ReminderPolicy& reminder_policy, ModeManager& mode_mgr,
                                     GimbalMonitor* gimbal_monitor,
                                     session::SessionOrchestrator* session_orchestrator,
                                     PhoneFactBuffer* phone_facts,
                                     session::SessionSnapshotPublisher* session_snapshots,
                                     AppEventOutbox* event_outbox,
                                     power::WorkloadProfilePolicy* workload_policy,
                                     gimbal::GimbalPrecheckController* gimbal_precheck,
                                     DeviceManager* device_manager)
    : store_(store)
    , sqlite_store_(sqlite_store)
    , reminder_policy_(reminder_policy)
    , mode_mgr_(mode_mgr)
    , gimbal_monitor_(gimbal_monitor)
    , session_orchestrator_(session_orchestrator)
    , phone_facts_(phone_facts)
    , session_snapshots_(session_snapshots)
    , event_outbox_(event_outbox)
    , workload_policy_(workload_policy)
    , gimbal_precheck_(gimbal_precheck)
    , device_manager_(device_manager) {
    if (session_snapshots_ && gimbal_precheck_) {
        gimbal_push_state_ = std::make_shared<GimbalPushState>();
        const auto push_state = gimbal_push_state_;
        auto* const state_store = &store_;
        session_snapshots_->setGimbalUpdateCallback(
            [push_state, state_store](const session::SessionSnapshot& snapshot) {
                if (snapshot.phase != session::Phase::Precheck ||
                    !snapshot.runtime_active) {
                    return;
                }
                auto data = gimbalPrecheckPushData(snapshot);
                const auto signature = data.dump();
                {
                    std::lock_guard<std::mutex> lock(push_state->mutex);
                    if (push_state->signature == signature) return;
                    push_state->signature = signature;
                }
                state_store->pushEvent(
                    AppCommandService::makePush("push_gimbal_precheck_state", data));
            });
    }
    if (gimbal_precheck_) {
        gimbal_command_worker_ = std::thread(
            &AppCommandService::gimbalCommandWorkerLoop, this);
    }
}

AppCommandService::~AppCommandService() {
    if (session_snapshots_) {
        session_snapshots_->setGimbalUpdateCallback({});
    }
    {
        std::lock_guard<std::mutex> lock(gimbal_command_mutex_);
        stop_gimbal_command_worker_ = true;
    }
    gimbal_command_cv_.notify_all();
    if (gimbal_command_worker_.joinable()) gimbal_command_worker_.join();
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
    prefs.system_mode = static_cast<int>(systemModeFromNotificationMode(prefs.notification_mode));
    prefs.away_timeout_min = parseIntOrDefault(sqlite_store_.getPreference("away_timeout_min", std::to_string(prefs.away_timeout_min)),
                                               prefs.away_timeout_min);
    prefs.default_focus_duration_min = clampMinutes(
        parseIntOrDefault(sqlite_store_.getPreference("default_focus_duration_min",
                                                      std::to_string(prefs.default_focus_duration_min)),
                          prefs.default_focus_duration_min),
        5, 120);
    prefs.sound_volume = clampFloat(
        parseFloatOrDefault(sqlite_store_.getPreference("sound_volume", std::to_string(prefs.sound_volume)),
                            prefs.sound_volume),
        0.0f, 1.0f);
    prefs.normal_channels_mask = sanitizeNotificationChannelMask(
        parseNotificationMaskOrDefault(
            sqlite_store_.getPreference("normal_channels_mask", std::to_string(prefs.normal_channels_mask)),
            prefs.normal_channels_mask), true);
    prefs.silent_channels_mask = sanitizeNotificationChannelMask(
        parseNotificationMaskOrDefault(
            sqlite_store_.getPreference("silent_channels_mask", std::to_string(prefs.silent_channels_mask)),
            prefs.silent_channels_mask), false);
    // Board-local display and sound are no longer product outputs. Ignore
    // persisted legacy "board" routes and expose App-owned output instead.
    prefs.screen_output_route = "phone";
    prefs.sound_output_route = "phone";
    // Gimbal availability is capability-driven. It is not a normal product
    // preference; verified hardware is enabled unless the maintenance switch
    // below explicitly disables it.
    prefs.gimbal_enabled = gimbal_precheck_ != nullptr;
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
        if (cmd == "client_hello")             return handleClientHello(data, req_id);
        if (cmd == "event_ack")                return handleEventAck(data, req_id);
        if (cmd == "set_power_profile")         return handleSetPowerProfile(data, req_id);
        if (cmd == "band_control")              return handleBandControl(data, req_id);
        if (cmd == "gimbal_manual_takeover" || cmd == "gimbal_lease_keepalive" ||
            cmd == "gimbal_nudge" || cmd == "gimbal_center" ||
            cmd == "gimbal_stop" || cmd == "gimbal_release" ||
            cmd == "gimbal_force_release" || cmd == "gimbal_recover" ||
            cmd == "precheck_skip_auto_adjust" ||
            cmd == "precheck_confirm_position") {
            return handleGimbalPrecheckCommand(cmd, data, req_id);
        }
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
        {"external_power_state", snap.device.external_power_state},
        {"external_power_present", externalPowerPresentWire(snap.device)},
        {"external_power_sample_age_ms", externalPowerSampleAgeWire(snap.device)},
        {"system_mode", static_cast<int>(snap.device.system_mode)},
        {"notification_mode", notificationModeToString(snap.device.notification_mode)},
        {"power_profile", powerProfileToString(snap.device.power_profile)},
        {"power_profile_source", snap.device.power_profile_source},
        {"manual_override_active", snap.device.manual_override_active},
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

void AppCommandService::applyManualPowerProfile(power::WorkloadProfile profile) {
    if (!workload_policy_) {
        throw std::runtime_error("power profile service unavailable");
    }
    workload_policy_->setManualOverride(profile);
    const auto power = workload_policy_->snapshot();
    auto device = store_.snapshot().device;
    device.power_profile = power.effective_profile == power::WorkloadProfile::Home
        ? PowerProfile::HOME
        : PowerProfile::GO;
    device.power_profile_source = power::profileSourceName(power.source);
    device.manual_override_active = power.manual_override_active;
    device.power_epoch = power.power_epoch;
    store_.updateDeviceStatus(device);
}

std::string AppCommandService::handleSetPowerProfile(const json& data, int req_id) {
    if (!data.contains("power_profile") || !data["power_profile"].is_string()) {
        throw std::invalid_argument("power_profile must be home or go");
    }
    const auto requested = lowerCopy(data["power_profile"].get<std::string>());
    if (requested == "home") {
        applyManualPowerProfile(power::WorkloadProfile::Home);
    } else if (requested == "go") {
        applyManualPowerProfile(power::WorkloadProfile::Go);
    } else {
        throw std::invalid_argument("power_profile must be home or go");
    }
    const auto device = store_.snapshot().device;
    return makeResponse(req_id, {
        {"power_profile", powerProfileToString(device.power_profile)},
        {"power_profile_source", device.power_profile_source},
        {"manual_override_active", device.manual_override_active},
        {"power_epoch", device.power_epoch}
    });
}

std::string AppCommandService::handleBandControl(const json& data, int req_id) {
    if (!device_manager_) return makeError(req_id, "Band control unavailable");
    const std::string action = lowerCopy(data.value("action", ""));
    if (action != "scan_connect" && action != "reconnect") {
        return makeError(req_id, "action must be scan_connect or reconnect");
    }
    const bool was_connected = device_manager_->isBandConnected();
    const bool accepted = device_manager_->requestBandConnection(action == "reconnect");
    if (!accepted) {
        return makeResponse(req_id, {
            {"accepted", false},
            {"operation", action},
            {"bracelet_connected", was_connected},
            {"message", "Band connection service is disabled"}
        });
    }
    return makeResponse(req_id, {
        {"accepted", true},
        {"operation", action},
        {"bracelet_connected", device_manager_->isBandConnected()},
        {"already_connected", was_connected && action == "scan_connect"}
    });
}

std::string AppCommandService::handleGimbalPrecheckCommand(
    const std::string& command, const json& data, int req_id) {
    if (!gimbal_precheck_ || !session_orchestrator_) {
        return makeError(req_id, "Gimbal PRECHECK feature disabled");
    }
    const auto phase = session_orchestrator_->phase();
    if (phase != session::Phase::Precheck && phase != session::Phase::Focusing) {
        return makeError(req_id,
                         "Gimbal control is only available during PRECHECK or FOCUSING");
    }
    if (!data.contains("session_id") || !data["session_id"].is_string()) {
        return makeError(req_id, "session_id is required");
    }
    if (!data.contains("sequence") || !data["sequence"].is_number_unsigned() ||
        data["sequence"].get<std::uint64_t>() == 0) {
        return makeError(req_id, "sequence must be a positive unsigned integer");
    }
    const bool identity_free = command == "precheck_skip_auto_adjust" ||
                               command == "gimbal_recover";
    const bool takeover = command == "gimbal_manual_takeover";
    const bool precheck_direct = phase == session::Phase::Precheck &&
        (command == "gimbal_nudge" || command == "gimbal_center" ||
         command == "gimbal_stop" || command == "precheck_confirm_position");
    if (!identity_free &&
        (!data.contains("owner_id") || !data["owner_id"].is_string())) {
        return makeError(req_id, "owner_id is required");
    }
    if (!identity_free && !takeover && !precheck_direct &&
        (!data.contains("lease_token") || !data["lease_token"].is_string())) {
        return makeError(req_id, "lease_token is required");
    }
    if (command == "gimbal_nudge" &&
        (!data.contains("direction") || !data["direction"].is_string())) {
        return makeError(req_id, "direction must be UP, DOWN, LEFT or RIGHT");
    }
    if (command == "gimbal_force_release" && !data.value("confirmed", false)) {
        return makeError(req_id, "gimbal_force_release requires confirmed=true");
    }

    {
        std::lock_guard<std::mutex> lock(gimbal_command_mutex_);
        constexpr std::size_t kMaxQueuedGimbalCommands = 32;
        if (gimbal_command_queue_.size() >= kMaxQueuedGimbalCommands) {
            return makeError(req_id, "gimbal_command_queue_full");
        }
        QueuedGimbalCommand queued{command, data, req_id};
        if (command == "gimbal_stop" || command == "gimbal_force_release") {
            gimbal_command_queue_.push_front(std::move(queued));
        } else {
            gimbal_command_queue_.push_back(std::move(queued));
        }
    }
    gimbal_command_cv_.notify_one();
    return makeResponse(req_id, {
        {"queued", true},
        {"command", command},
        {"request_sequence", data["sequence"]},
        {"session_id", data["session_id"]}
    });
}

void AppCommandService::gimbalCommandWorkerLoop() {
    while (true) {
        QueuedGimbalCommand queued;
        {
            std::unique_lock<std::mutex> lock(gimbal_command_mutex_);
            gimbal_command_cv_.wait(lock, [this] {
                return stop_gimbal_command_worker_ || !gimbal_command_queue_.empty();
            });
            if (stop_gimbal_command_worker_) {
                gimbal_command_queue_.clear();
                return;
            }
            queued = std::move(gimbal_command_queue_.front());
            gimbal_command_queue_.pop_front();
        }

        json event_data = {
            {"command", queued.command},
            {"request_id", queued.request_id},
            {"request_sequence", queued.data.value("sequence", std::uint64_t{})},
            {"session_id", queued.data.value("session_id", "")},
            {"owner_id", queued.data.value("owner_id", "")}
        };
        try {
            const auto response_text = executeGimbalPrecheckCommand(
                queued.command, queued.data, queued.request_id);
            const auto response = json::parse(response_text);
            const bool accepted = response.value("status", "error") == "success";
            event_data["accepted"] = accepted;
            if (accepted && response.contains("data") && response["data"].is_object()) {
                for (const auto& item : response["data"].items()) {
                    event_data[item.key()] = item.value();
                }
                if (event_data.value("firmware_accepted", false)) {
                    event_data["acked_through_sequence"] =
                        queued.data.value("sequence", std::uint64_t{});
                }
            } else {
                event_data["error"] = response.value("error", "gimbal_command_failed");
            }
        } catch (const std::exception& e) {
            event_data["accepted"] = false;
            event_data["error"] = e.what();
        } catch (...) {
            event_data["accepted"] = false;
            event_data["error"] = "unknown_gimbal_command_error";
        }
        store_.pushEvent(makePush("push_gimbal_command_result", event_data));
    }
}

std::string AppCommandService::executeGimbalPrecheckCommand(
    const std::string& command, const json& data, int req_id) {
    if (!gimbal_precheck_ || !session_orchestrator_) {
        return makeError(req_id, "Gimbal PRECHECK feature disabled");
    }
    const auto phase = session_orchestrator_->phase();
    if (phase != session::Phase::Precheck && phase != session::Phase::Focusing) {
        return makeError(req_id, "Gimbal control is only available during PRECHECK or FOCUSING");
    }
    if (!data.contains("session_id") || !data["session_id"].is_string()) {
        return makeError(req_id, "session_id is required");
    }
    if (!data.contains("sequence") || !data["sequence"].is_number_unsigned()) {
        return makeError(req_id, "sequence must be a positive unsigned integer");
    }
    const auto session_id = data["session_id"].get<std::string>();
    const auto sequence = data["sequence"].get<std::uint64_t>();
    const auto now_ms = monotonicMs();
    gimbal::CommandResult result;
    if (command == "precheck_skip_auto_adjust") {
        if (phase != session::Phase::Precheck) {
            return makeError(req_id, "Automatic adjustment can only be skipped during PRECHECK");
        }
        result = gimbal_precheck_->skipAuto(session_id, sequence, now_ms);
    } else if (command == "gimbal_recover") {
        result = gimbal_precheck_->realign(session_id, sequence, now_ms);
    } else if (command == "gimbal_manual_takeover") {
        if (phase != session::Phase::Focusing) {
            return makeError(req_id,
                             "Manual takeover is only available during FOCUSING");
        }
        if (!data.contains("owner_id") || !data["owner_id"].is_string()) {
            return makeError(req_id, "owner_id is required");
        }
        result = gimbal_precheck_->manualTakeover(
            session_id, data["owner_id"].get<std::string>(), sequence,
            phase, now_ms);
    } else {
        if (!data.contains("owner_id") || !data["owner_id"].is_string()) {
            return makeError(req_id, "owner_id is required");
        }
        const bool precheck_direct = phase == session::Phase::Precheck &&
            (command == "gimbal_nudge" || command == "gimbal_center" ||
             command == "gimbal_stop" || command == "precheck_confirm_position");
        if (!precheck_direct &&
            (!data.contains("lease_token") || !data["lease_token"].is_string())) {
            return makeError(req_id, "lease_token is required");
        }
        const gimbal::ManualCommandContext lease{
            session_id, data["owner_id"].get<std::string>(),
            data.value("lease_token", ""), sequence};
        if (command == "gimbal_lease_keepalive") {
            result = gimbal_precheck_->keepManualLease(lease, phase, now_ms);
        } else if (command == "gimbal_nudge") {
            if (!data.contains("direction") || !data["direction"].is_string()) {
                return makeError(req_id, "direction must be UP, DOWN, LEFT or RIGHT");
            }
            const auto direction = lowerCopy(data["direction"].get<std::string>());
            gimbal::NudgeDirection parsed;
            if (direction == "left") parsed = gimbal::NudgeDirection::Left;
            else if (direction == "right") parsed = gimbal::NudgeDirection::Right;
            else if (direction == "up") parsed = gimbal::NudgeDirection::Up;
            else if (direction == "down") parsed = gimbal::NudgeDirection::Down;
            else return makeError(req_id, "direction must be UP, DOWN, LEFT or RIGHT");
            if (data.contains("fine_tune") && !data["fine_tune"].is_boolean()) {
                return makeError(req_id, "fine_tune must be boolean");
            }
            result = gimbal_precheck_->nudge(
                lease, parsed, now_ms, data.value("fine_tune", false));
        } else if (command == "gimbal_center") {
            result = gimbal_precheck_->center(lease, now_ms);
        } else if (command == "gimbal_stop") {
            if (phase == session::Phase::Precheck) {
            result = gimbal_precheck_->forceReleaseOperator(
                    session_id, lease.owner_id, sequence, now_ms);
            } else {
                result = gimbal_precheck_->stopMotion(lease, now_ms);
            }
        } else if (command == "gimbal_release") {
            result = gimbal_precheck_->releaseManualControl(lease, phase, now_ms);
        } else if (command == "gimbal_force_release") {
            if (!data.value("confirmed", false)) {
                return makeError(req_id, "gimbal_force_release requires confirmed=true");
            }
            result = gimbal_precheck_->forceRelease(lease, phase, now_ms);
        } else {
            result = gimbal_precheck_->confirmPosition(lease, phase, now_ms);
        }
    }
    if (!result.success) {
        if (result.motion_status == "boundary_pending") {
            return makeResponse(req_id, {
                {"accepted", false},
                {"error", result.error},
                {"fine_tune", result.fine_tune},
                {"resolved_step_deg", result.resolved_step_deg},
                {"motion_status", result.motion_status},
                {"boundary_stop_planned", result.boundary_stop_planned},
                {"boundary_paused", result.boundary_paused}
            });
        }
        return makeError(req_id, result.error.empty() ? "Gimbal command failed" : result.error);
    }
    const auto status = gimbal_precheck_->snapshot();
    const auto hardware = gimbal_precheck_->snapshotHardware();
    return makeResponse(req_id, {
        {"accepted", true},
        {"state", gimbal::precheckStateName(status.state)},
        {"manual_lease", status.manual_lease},
        {"lease_owner_id", status.lease_owner_id},
        {"lease_token", status.lease_token},
        {"lease_epoch", status.lease_epoch},
        {"lease_expires_at", status.lease_expires_at_ms},
        {"search_generation", status.generation},
        {"waypoint_index", status.waypoint_index},
        {"subject_found", status.subject_found},
        {"search_completed", status.search_completed},
        {"queued", result.queued},
        {"firmware_accepted", result.firmware_accepted},
        {"settled", result.settled},
        {"fine_tune", result.fine_tune},
        {"resolved_step_deg", result.resolved_step_deg},
        {"motion_status", result.motion_status},
        {"boundary_stop_planned", result.boundary_stop_planned},
        {"boundary_paused", result.boundary_paused},
        {"axes", {
            {"yaw", {{"decision", result.yaw_decision}}},
            {"pitch", {{"decision", result.pitch_decision}}}
        }},
        {"motion_generation", result.motion_generation
             ? json(*result.motion_generation) : json(nullptr)},
        {"target_yaw10", hardware.snapshot.target_yaw10
             ? json(*hardware.snapshot.target_yaw10) : json(nullptr)},
        {"target_pitch10", hardware.snapshot.target_pitch10
             ? json(*hardware.snapshot.target_pitch10) : json(nullptr)},
        {"detail", status.detail}
    });
}

std::string AppCommandService::handleGetCurrentState(int req_id) {
    auto snap = store_.snapshot();
    const bool gimbal_precheck_available = gimbalPrecheckAvailable(snap, gimbal_precheck_);
    json data = sessionStateData(snap, gimbal_precheck_available);
    data["reminder"] = reminderData(snap);
    data["precheck_guidance"] = precheckGuidanceData(snap);
    if (session_snapshots_) {
        data["runtime_snapshot"] = stableSessionSnapshotData(
            session_snapshots_->read(monotonicMs()));
    }
    if (event_outbox_) {
        data["event_delivery"] = event_outbox_->snapshot(monotonicMs());
    }
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
    if (session_orchestrator_) return handleSessionControlV2(data, req_id);
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
            gimbalPrecheckAvailable(started_snapshot, gimbal_precheck_));
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
            json resp = sessionStateData(snap, gimbalPrecheckAvailable(snap, gimbal_precheck_));
            resp["success"] = false;
            resp["message"] = "App is not allowed to skip board-owned precheck";
            return makeResponse(req_id, resp);
        }
        SessionController controller(store_, sqlite_store_);
        bool ok = controller.skipPrecheck("app_skip_precheck");
        auto after = store_.snapshot();
        const bool gimbal_precheck_available = gimbalPrecheckAvailable(after, gimbal_precheck_);
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
        const bool gimbal_precheck_available = gimbalPrecheckAvailable(after, gimbal_precheck_);
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
        const bool gimbal_precheck_available = gimbalPrecheckAvailable(after, gimbal_precheck_);
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

std::string AppCommandService::handleSessionControlV2(const json& data, int req_id) {
    const std::string action = data.value("action", "");
    const auto before = store_.snapshot();
    session::SessionOrchestrator::Result result;

    if (action == "start") {
        const int target_min = targetDurationMinutesFromPayload(data, before.preferences);
        const std::string source = lowerCopy(data.value("source_mode", "live_camera"));
        if (source != "live_camera") {
            return makeError(req_id, "App sessions only support live_camera");
        }
        result = session_orchestrator_->start(
            {minutesToMs(target_min), session::SourceMode::LiveCamera,
             data.value("operation_id", std::string{})});
        auto after = store_.snapshot();
        json response = sessionStateData(after, gimbalPrecheckAvailable(after, gimbal_precheck_));
        response["success"] = result.success;
        response["already_active"] = result.already_active;
        if (session_snapshots_) {
            response["runtime_snapshot"] = stableSessionSnapshotData(
                session_snapshots_->read(monotonicMs()));
        }
        if (!result.error.empty()) response["message"] = result.error;
        return makeResponse(req_id, response);
    }

    if (data.contains("session_id")) {
        if (!data["session_id"].is_string()) {
            return makeError(req_id, "session_id must be a string");
        }
        const std::string expected_session_id = data["session_id"].get<std::string>();
        const auto runtime = session_snapshots_
                                 ? session_snapshots_->read(monotonicMs())
                                 : session::SessionSnapshot{};
        const std::string active_session_id = session_snapshots_
                                                  ? runtime.context.session_id
                                                  : before.session.session_id;
        if (expected_session_id.empty() || expected_session_id != active_session_id) {
            json response = sessionStateData(
                before, gimbalPrecheckAvailable(before, gimbal_precheck_));
            response["success"] = false;
            response["message"] = "session_id_mismatch";
            if (session_snapshots_) {
                response["runtime_snapshot"] = stableSessionSnapshotData(runtime);
            }
            return makeResponse(req_id, response);
        }
    }

    if (action == "skip_precheck" || action == "skip_calibration") {
        result = session_orchestrator_->confirmReady();
    } else if (action == "pause") {
        result = session_orchestrator_->pause();
    } else if (action == "resume") {
        result = session_orchestrator_->resume();
    } else if (action == "end") {
        const auto runtime_before = session_snapshots_
                                        ? session_snapshots_->read(monotonicMs())
                                        : session::SessionSnapshot{};
        const bool was_formal = session_snapshots_ ? runtime_before.formal_active
                                                   : before.session.active;
        const auto target_duration_ms = session_snapshots_
                                            ? runtime_before.context.target_duration_ms
                                            : before.session.target_duration_ms;
        result = session_orchestrator_->end(
            was_formal ? session::StopReason::Completed : session::StopReason::Cancelled);
        const auto ended_snapshot = store_.snapshot();
        const auto& report = ended_snapshot.last_report;
        const bool report_matches = result.success && was_formal &&
            !report.session_id.empty() && result.context &&
            report.session_id == result.context->session_id;
        if (report_matches) {
            const json push_data = sessionReportData(report, target_duration_ms, "user_ended");
            store_.pushEvent(makePush("push_session_end", push_data));
        }
        json response = report_matches
                            ? sessionReportData(report, target_duration_ms, "user_ended")
                            : json{{"formal_session_started", was_formal},
                                   {"cancelled_precheck", result.success && !was_formal},
                                   {"report_available", false}};
        response["success"] = result.success;
        if (session_snapshots_) {
            response["runtime_snapshot"] = stableSessionSnapshotData(
                session_snapshots_->read(monotonicMs()));
        }
        if (!result.error.empty()) response["message"] = result.error;
        return makeResponse(req_id, response);
    } else {
        return makeError(req_id, "Unknown action: " + action);
    }

    const auto after = store_.snapshot();
    const bool gimbal_available = gimbalPrecheckAvailable(after, gimbal_precheck_);
    json response = sessionStateData(after, gimbal_available);
    response["success"] = result.success;
    if (session_snapshots_) {
        response["runtime_snapshot"] = stableSessionSnapshotData(
            session_snapshots_->read(monotonicMs()));
    }
    if (!result.error.empty()) response["message"] = result.error;
    if (result.success) {
        store_.pushEvent(makePush("push_state", sessionStateData(after, gimbal_available)));
    }
    return makeResponse(req_id, response);
}

std::string AppCommandService::handleUpdatePreferences(const json& data, int req_id) {
    if (data.contains("power_profile")) {
        throw std::invalid_argument("use set_power_profile to change Home/Go mode");
    }
    Preferences prefs = store_.snapshot().preferences;

    if (data.contains("system_mode")) {
        int mode = data["system_mode"].get<int>();
        if (!isValidSystemMode(mode)) {
            throw std::invalid_argument("invalid system_mode");
        }
        prefs.notification_mode = notificationModeFromLegacySystemMode(mode);
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
    prefs.system_mode = static_cast<int>(systemModeFromNotificationMode(prefs.notification_mode));
    mode_mgr_.setMode(systemModeFromNotificationMode(prefs.notification_mode));
    DeviceStatus ds = store_.snapshot().device;
    ds.system_mode = systemModeFromNotificationMode(prefs.notification_mode);
    ds.notification_mode = prefs.notification_mode;
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
    if (data.contains("sound_volume")) {
        prefs.sound_volume = clampFloat(data["sound_volume"].get<float>(), 0.0f, 1.0f);
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
    // Accepted for protocol compatibility, but board-local output cannot be
    // re-enabled by an old App or stale preference record.
    prefs.screen_output_route = "phone";
    prefs.sound_output_route = "phone";
    // Keep gimbal ownership capability-driven. App/Web preference updates may
    // not bypass board capability detection or disable a verified controller.
    prefs.gimbal_enabled = gimbal_precheck_ != nullptr;
    prefs = sanitizePreferences(prefs);

    store_.updatePreferences(prefs);

    sqlite_store_.savePreference("system_mode", std::to_string(prefs.system_mode));
    sqlite_store_.savePreference("notification_mode", notificationModeToString(prefs.notification_mode));
    sqlite_store_.savePreference("away_timeout_min", std::to_string(prefs.away_timeout_min));
    sqlite_store_.savePreference("default_focus_duration_min", std::to_string(prefs.default_focus_duration_min));
    sqlite_store_.savePreference("sound_volume", std::to_string(prefs.sound_volume));
    sqlite_store_.savePreference("normal_channels_mask", std::to_string(prefs.normal_channels_mask));
    sqlite_store_.savePreference("silent_channels_mask", std::to_string(prefs.silent_channels_mask));
    sqlite_store_.savePreference("screen_output_route", prefs.screen_output_route);
    sqlite_store_.savePreference("sound_output_route", prefs.sound_output_route);
    sqlite_store_.savePreference("gimbal_enabled", prefs.gimbal_enabled ? "1" : "0");

    json resp = {
        {"system_mode", prefs.system_mode},
        {"notification_mode", notificationModeToString(prefs.notification_mode)},
        {"away_timeout_min", prefs.away_timeout_min},
        {"default_focus_duration_min", prefs.default_focus_duration_min},
        {"sound_volume", prefs.sound_volume},
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

std::string AppCommandService::handleClientHello(const json& data, int req_id) {
    if (!event_outbox_) return makeError(req_id, "reliable event transport unavailable");
    std::vector<std::string> capabilities;
    if (data.contains("capabilities")) {
        if (!data["capabilities"].is_array()) {
            return makeError(req_id, "capabilities must be an array");
        }
        for (const auto& item : data["capabilities"]) {
            if (item.is_string()) capabilities.push_back(item.get<std::string>());
        }
    }
    const bool reliable = event_outbox_->negotiate(capabilities);
    json server_capabilities = json::array({
        "event_ack_v1", "event_dedupe_v1", "phone_heartbeat_v1",
        "external_power_v1", "power_profile_override_v1"});
    if (gimbal_precheck_) {
        server_capabilities.push_back("gimbal_session_control_v3");
        server_capabilities.push_back("gimbal_manual_lease_v1");
        server_capabilities.push_back("gimbal_async_result_v1");
    }
    if (device_manager_) server_capabilities.push_back("band_proxy_control_v1");
    return makeResponse(req_id, {{"schema_version", "2.1"},
                                 {"reliable_push", reliable},
                                 {"phone_fresh_for_ms", 5000},
                                 {"server_capabilities", server_capabilities}});
}

std::string AppCommandService::handleEventAck(const json& data, int req_id) {
    if (!event_outbox_) return makeError(req_id, "reliable event transport unavailable");
    const std::string event_id = data.value("event_id", "");
    const std::string session_id = data.value("session_id", "");
    const std::string stage = lowerCopy(data.value("stage", ""));
    if (event_id.empty() || session_id.empty() || stage.empty()) {
        return makeError(req_id, "event_id, session_id and stage are required");
    }
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto result = event_outbox_->acknowledge(event_id, session_id, stage, now_ms);
    if ((result.status == "accepted" || result.status == "duplicate") &&
        result.delivered_channels_mask != 0) {
        auto snap = store_.snapshot();
        auto device = snap.device;
        device.last_delivered_channels_mask |= result.delivered_channels_mask;
        store_.updateDeviceStatus(device);
    }
    if (result.status == "accepted" && stage == "user_ack") {
        reminder_policy_.onUserAck(now_ms);
    }
    return makeResponse(req_id, {{"ack_status", result.status},
                                 {"event_id", result.event_id},
                                 {"stage", result.stage}});
}

std::string AppCommandService::handlePhoneSync(const json& data, int req_id) {
    auto parsed = parsePhoneSyncPayload(data);

    auto snap = store_.snapshot();

    if (phone_facts_) {
        const auto runtime = session_snapshots_
                                 ? session_snapshots_->read(monotonicMs())
                                 : session::SessionSnapshot{};
        if (session_snapshots_ && runtime.runtime_active && !runtime.formal_active) {
            return makeResponse(req_id, {
                {"accepted", false},
                {"error_code", "formal_session_not_started"},
                {"message", "phone facts are accepted after PRECHECK confirmation"},
                {"session_id", runtime.context.session_id}
            });
        }
        session::PhoneFact fact;
        const std::string active_session_id =
            session_snapshots_ && runtime.formal_active
                ? runtime.context.session_id
                : snap.session.session_id;
        fact.session_id = data.value("session_id", active_session_id);
        fact.sequence = data.value("sequence", ++phone_sequence_);
        fact.received_monotonic_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        fact.source_timestamp_ms = data.value("timestamp_ms", fact.received_monotonic_ms);
        fact.active = parsed.active;
        fact.fresh_for_ms = 5000;
        if (parsed.active) {
            if (parsed.usage_state == PhoneUsageState::LEARNING) {
                fact.category = session::PhoneCategory::Learning;
            } else if (parsed.usage_state == PhoneUsageState::PLAYING) {
                fact.category = session::PhoneCategory::Entertainment;
            } else {
                fact.category = session::PhoneCategory::GeneralOrUnknown;
            }
        }
        std::string error;
        const bool accepted = phone_facts_->publish(fact, &error);
        json response = {
            {"accepted", accepted},
            {"sequence", fact.sequence},
            {"last_accepted_sequence", phone_facts_->snapshot(fact.received_monotonic_ms).sequence},
            {"fresh_for_ms", fact.fresh_for_ms},
            {"category_source", "app_raw_fact"},
            {"distraction_level_ignored", data.contains("distraction_level")}
        };
        if (!error.empty()) response["message"] = error;
        return makeResponse(req_id, response);
    }

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
            {"external_power_state", snap.device.external_power_state},
            {"external_power_present", externalPowerPresentWire(snap.device)},
            {"external_power_sample_age_ms", externalPowerSampleAgeWire(snap.device)},
            {"power_profile", powerProfileWire(snap.device.power_profile)},
            {"power_profile_source", snap.device.power_profile_source},
            {"manual_override_active", snap.device.manual_override_active},
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
    if (event_outbox_) {
        data["event_delivery"] = event_outbox_->snapshot(monotonicMs());
    }
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
        {"away_timeout_min", snap.preferences.away_timeout_min},
        {"default_focus_duration_min", snap.preferences.default_focus_duration_min},
        {"sound_volume", snap.preferences.sound_volume},
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
    json status = gimbalStatusData(snap);
    if (gimbal_precheck_) {
        const auto precheck = gimbal_precheck_->snapshot();
        const auto hardware = gimbal_precheck_->snapshotHardware();
        const bool online = hardware.link_healthy;
        status["connected"] = online;
        status["online"] = online;
        status["safe"] = online && !hardware.fault_active &&
            precheck.state != gimbal::PrecheckState::Degraded;
        status["calibrated"] = hardware.state == gimbal::State::Ready ||
            hardware.state == gimbal::State::Moving ||
            hardware.state == gimbal::State::Holding;
        status["angle_x"] = hardware.yaw_deg ? json(*hardware.yaw_deg) : json(nullptr);
        status["angle_y"] = hardware.pitch_deg ? json(*hardware.pitch_deg) : json(nullptr);
        status["last_error"] = hardware.fault_latched_reason;
        status["state"] = gimbal::precheckStateName(precheck.state);
        status["manual_lease"] = precheck.manual_lease;
        status["lease_owner_id"] = precheck.lease_owner_id;
        status["lease_epoch"] = precheck.lease_epoch;
        status["lease_expires_at"] = precheck.lease_expires_at_ms;
        status["search_completed"] = precheck.search_completed;
        status["protocol_version"] = hardware.snapshot.protocol_version;
        status["capabilities"] = hardware.snapshot.capabilities;
        status["available"] = hardware.snapshot.available;
        status["degraded_reason"] = hardware.snapshot.degraded_reason;
        switch (hardware.snapshot.link_health) {
            case gimbal::LinkHealth::Unavailable: status["link_health"] = "unavailable"; break;
            case gimbal::LinkHealth::Negotiating: status["link_health"] = "negotiating"; break;
            case gimbal::LinkHealth::Healthy: status["link_health"] = "healthy"; break;
            case gimbal::LinkHealth::Degraded: status["link_health"] = "degraded"; break;
        }
        status["control_mode"] = gimbal::controlModeName(hardware.snapshot.control_mode);
        status["current_yaw10"] = hardware.snapshot.current_yaw10
            ? json(*hardware.snapshot.current_yaw10) : json(nullptr);
        status["current_pitch10"] = hardware.snapshot.current_pitch10
            ? json(*hardware.snapshot.current_pitch10) : json(nullptr);
        status["target_yaw10"] = hardware.snapshot.target_yaw10
            ? json(*hardware.snapshot.target_yaw10) : json(nullptr);
        status["target_pitch10"] = hardware.snapshot.target_pitch10
            ? json(*hardware.snapshot.target_pitch10) : json(nullptr);
        status["motion_generation"] = hardware.snapshot.motion_generation
            ? json(*hardware.snapshot.motion_generation) : json(nullptr);
        status["settled"] = hardware.snapshot.settled;
        status["calibration_valid"] = hardware.snapshot.calibration_valid;
        status["calibration_epoch"] = hardware.snapshot.calibration_epoch;
        status["origin_valid"] = hardware.snapshot.origin_valid;
        status["origin_epoch"] = hardware.snapshot.origin_epoch;
        status["fault_class"] = gimbal::errorClassName(hardware.snapshot.fault_class);
        status["fault_code"] = hardware.snapshot.fault_code;
        status["last_pose_age_ms"] = hardware.snapshot.last_pose_age_ms;
        status["last_queued_sequence"] = hardware.snapshot.last_queued_sequence;
        status["acked_through_sequence"] = hardware.snapshot.acked_through_sequence;
        status["current_protection_state"] = hardware.snapshot.current_protection_state;
        status["maintenance_operation"] = hardware.snapshot.operation;
        status["maintenance_operation_generation"] = hardware.snapshot.operation_generation
            ? json(*hardware.snapshot.operation_generation) : json(nullptr);
        status["maintenance_operation_progress"] = hardware.snapshot.operation_progress
            ? json(*hardware.snapshot.operation_progress) : json(nullptr);
        status["last_maintenance_operation_status"] =
            hardware.snapshot.last_operation_status;
        status["detail"] = precheck.detail;
    }
    return makeResponse(req_id, status);
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
        if (!snap.session.active || deriveSessionPhase(snap.session) != SessionPhase::PRECHECK) {
            return makeError(req_id, "Manual gimbal motion is only available during precheck");
        }
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
