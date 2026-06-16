#include "BleGattServer.h"
#include "app/AppContext.h"
#include "app/Logger.h"
#include "behavior/EventDetector.h"

#include <chrono>
#include <sstream>
#include <algorithm>

namespace edgefocus {

BleGattServer::BleGattServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                             ReminderPolicy& reminder_policy, ModeManager& mode_mgr,
                             ReportGenerator& report_gen)
    : Module("BleGattServer")
    , store_(store)
    , sqlite_store_(sqlite_store)
    , reminder_policy_(reminder_policy)
    , mode_mgr_(mode_mgr)
    , report_gen_(report_gen) {}

BleGattServer::~BleGattServer() = default;

bool BleGattServer::init(AppContext& /*ctx*/) {
    LOGI("BleGattServer", "BLE GATT Server initialized");
    return true;
}

void BleGattServer::runOnce(AppContext& /*ctx*/) {
    // On Linux, this would poll D-Bus for incoming writes to TX characteristic
    // and handle BLE events. On Windows, this is a no-op.
#ifdef __linux__
    // TODO: BlueZ D-Bus GATT server implementation
    // poll_dbus_events();
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void BleGattServer::shutdown(AppContext& /*ctx*/) {
    LOGI("BleGattServer", "BLE GATT Server shutdown");
}

// ---------------------------------------------------------------------------
// JSON helpers (minimal, no external library)
// ---------------------------------------------------------------------------

std::string BleGattServer::jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string BleGattServer::jsonResponse(bool ok, const std::string& data) {
    if (ok) {
        return "{\"status\":\"ok\",\"data\":" + data + "}";
    } else {
        return "{\"status\":\"error\",\"message\":\"" + jsonEscape(data) + "\"}";
    }
}

std::string BleGattServer::getJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

int BleGattServer::getJsonInt(const std::string& json, const std::string& key, int default_val) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return default_val;
    pos++;
    while (pos < json.size() && json[pos] == ' ') pos++;
    std::string num;
    while (pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))) {
        num += json[pos++];
    }
    if (num.empty()) return default_val;
    try { return std::stoi(num); } catch (...) { return default_val; }
}

bool BleGattServer::getJsonBool(const std::string& json, const std::string& key, bool default_val) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return default_val;
    pos++;
    while (pos < json.size() && json[pos] == ' ') pos++;
    return json.substr(pos, 4) == "true";
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------

std::string BleGattServer::handleCommand(const std::string& json_cmd) {
    std::string cmd = getJsonString(json_cmd, "cmd");
    LOGI("BleGattServer", "Received command: %s", cmd.c_str());

    if (cmd == "get_device_status")     return handleGetDeviceStatus();
    if (cmd == "get_current_state")     return handleGetCurrentState();
    if (cmd == "get_today_summary")     return handleGetTodaySummary();
    if (cmd == "get_last_report")       return handleGetLastReport();
    if (cmd == "session_control")       return handleSessionControl(json_cmd);
    if (cmd == "update_preferences")    return handleUpdatePreferences(json_cmd);
    if (cmd == "reminder_ack")          return handleReminderAck(json_cmd);
    if (cmd == "phone_sync")            return handlePhoneSync(json_cmd);
    if (cmd == "get_console_data")      return handleGetConsoleData();

    return jsonResponse(false, "Unknown command: " + cmd);
}

// ---------------------------------------------------------------------------
// API command implementations
// ---------------------------------------------------------------------------

std::string BleGattServer::handleGetDeviceStatus() {
    auto snap = store_.snapshot();
    std::string data = "{";
    data += "\"battery_percent\":" + std::to_string(snap.device.battery_percent) + ",";
    data += "\"system_mode\":" + std::to_string(static_cast<int>(snap.device.system_mode)) + ",";
    data += "\"camera_ok\":" + std::string(snap.device.camera_ok ? "true" : "false") + ",";
    data += "\"bracelet_connected\":" + std::string(snap.device.bracelet_connected ? "true" : "false") + ",";
    data += "\"is_session_active\":" + std::string(snap.session.active ? "true" : "false");
    data += "}";
    return jsonResponse(true, data);
}

std::string BleGattServer::handleGetCurrentState() {
    auto snap = store_.snapshot();
    std::string data = "{";
    data += "\"focus_state\":" + std::to_string(static_cast<int>(snap.focus_state)) + ",";
    data += "\"elapsed_ms\":" + std::to_string(snap.session.elapsed_ms) + ",";
    data += "\"target_duration_ms\":" + std::to_string(snap.session.target_duration_ms) + ",";
    data += "\"remaining_ms\":" + std::to_string(snap.session.remaining_ms) + ",";
    data += "\"distraction_count\":" + std::to_string(snap.session.distraction_count) + ",";
    data += "\"reminder_count\":" + std::to_string(snap.session.reminder_count) + ",";
    data += "\"calibration_completed\":" + std::string(snap.session.calibration_completed ? "true" : "false");
    data += "}";
    return jsonResponse(true, data);
}

std::string BleGattServer::handleGetTodaySummary() {
    auto snap = store_.snapshot();
    std::string data = "{";
    data += "\"total_seconds\":" + std::to_string(snap.today.total_seconds) + ",";
    data += "\"focus_seconds\":" + std::to_string(snap.today.focus_seconds) + ",";
    data += "\"sessions\":" + std::to_string(snap.today.sessions) + ",";
    data += "\"events\":" + std::to_string(snap.today.events) + ",";
    data += "\"phone_distraction_count\":" + std::to_string(snap.today.phone_distraction_count);
    data += "}";
    return jsonResponse(true, data);
}

std::string BleGattServer::handleGetLastReport() {
    auto snap = store_.snapshot();
    if (snap.last_report.session_id.empty()) {
        return jsonResponse(true, "null");
    }
    const auto& r = snap.last_report;
    std::string data = "{";
    data += "\"session_id\":\"" + jsonEscape(r.session_id) + "\",";
    data += "\"total_duration\":" + std::to_string(r.total_duration) + ",";
    data += "\"focus_duration\":" + std::to_string(r.focus_duration) + ",";
    data += "\"distraction_count\":" + std::to_string(r.distraction_count) + ",";
    data += "\"reminder_count\":" + std::to_string(r.reminder_count) + ",";
    data += "\"longest_focus_streak_ms\":" + std::to_string(r.longest_focus_streak_ms) + ",";
    data += "\"phone_distraction_count\":" + std::to_string(r.phone_distraction_count);
    data += "}";
    return jsonResponse(true, data);
}

std::string BleGattServer::handleSessionControl(const std::string& json_params) {
    std::string action = getJsonString(json_params, "action");
    auto snap = store_.snapshot();

    if (action == "start") {
        if (snap.session.active) {
            return jsonResponse(false, "Session already active");
        }
        SessionInfo s;
        s.active = true;
        s.calibrating = true;
        s.calibration_completed = false;
        s.session_id = "session_" + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        s.start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int target_min = getJsonInt(json_params, "target_duration_min", 0);
        if (target_min > 0) {
            s.target_duration_ms = target_min * 60000;
        }
        store_.updateSession(s);
        store_.notifySessionChange();
        LOGI("BleGattServer", "Session started: %s", s.session_id.c_str());
        return jsonResponse(true, "{\"session_id\":\"" + jsonEscape(s.session_id) + "\"}");

    } else if (action == "pause") {
        if (!snap.session.active) {
            return jsonResponse(false, "No active session");
        }
        SessionInfo s = snap.session;
        s.active = false;
        store_.updateSession(s);
        store_.notifySessionChange();
        return jsonResponse(true, "{}");

    } else if (action == "end") {
        if (!snap.session.active) {
            return jsonResponse(false, "No active session");
        }
        SessionInfo s = snap.session;
        s.active = false;
        store_.updateSession(s);
        store_.notifySessionChange();

        // Generate report
        if (!s.session_id.empty()) {
            SessionReport report = report_gen_.generateReport(s.session_id);
            store_.updateLastReport(report);
        }
        store_.resetSession();
        return jsonResponse(true, "{}");
    }

    return jsonResponse(false, "Unknown action: " + action);
}

std::string BleGattServer::handleUpdatePreferences(const std::string& json_params) {
    int mode = getJsonInt(json_params, "system_mode", -1);
    if (mode >= 0) {
        mode_mgr_.setMode(static_cast<SystemMode>(mode));
        DeviceStatus ds = store_.snapshot().device;
        ds.system_mode = static_cast<SystemMode>(mode);
        store_.updateDeviceStatus(ds);
    }

    int timeout_min = getJsonInt(json_params, "away_timeout_min", -1);
    if (timeout_min > 0) {
        SessionInfo s = store_.snapshot().session;
        s.away_timeout_ms = timeout_min * 60000;
        store_.updateSession(s);
    }

    auto snap = store_.snapshot();
    std::string data = "{";
    data += "\"system_mode\":" + std::to_string(static_cast<int>(snap.device.system_mode)) + ",";
    data += "\"away_timeout_min\":" + std::to_string(static_cast<int>(snap.session.away_timeout_ms / 60000));
    data += "}";
    return jsonResponse(true, data);
}

std::string BleGattServer::handleReminderAck(const std::string& /*json_params*/) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    reminder_policy_.onUserAck(now_ms);
    std::string data = "{\"cooling_ms\":15000}";
    return jsonResponse(true, data);
}

std::string BleGattServer::handlePhoneSync(const std::string& json_params) {
    bool is_in_use = getJsonBool(json_params, "is_in_use", false);
    int level = getJsonInt(json_params, "distraction_level", 3);

    if (!is_in_use) {
        // Clear phone signal
        PhoneUsageSignal sig;
        sig.active = false;
        sig.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        reminder_policy_.getModeManager(); // ensure initialized
        // Inject via store update - BehaviorEngine will pick up from state
        auto snap = store_.snapshot();
        std::string data = "{\"focus_state\":" + std::to_string(static_cast<int>(snap.focus_state)) + ",\"action_taken\":\"cleared\"}";
        return jsonResponse(true, data);
    }

    // Phone sync is handled by BleGattServer calling injectPhoneSignal on BehaviorEngine
    // For now, store the signal info for BehaviorEngine to pick up
    auto snap = store_.snapshot();
    std::string action = "none";
    if (level == 0) action = "learning_no_intervention";
    else if (level == 1) action = "weak_distraction";
    else if (level == 2) action = "strong_distraction";
    else action = "unclassified";

    std::string data = "{";
    data += "\"focus_state\":" + std::to_string(static_cast<int>(snap.focus_state)) + ",";
    data += "\"action_taken\":\"" + action + "\"";
    data += "}";
    return jsonResponse(true, data);
}

std::string BleGattServer::handleGetConsoleData() {
    auto snap = store_.snapshot();
    std::string data = "{";
    data += "\"session\":{";
    data += "\"active\":" + std::string(snap.session.active ? "true" : "false") + ",";
    data += "\"elapsed_ms\":" + std::to_string(snap.session.elapsed_ms) + ",";
    data += "\"target_duration_ms\":" + std::to_string(snap.session.target_duration_ms) + ",";
    data += "\"remaining_ms\":" + std::to_string(snap.session.remaining_ms) + ",";
    data += "\"distraction_count\":" + std::to_string(snap.session.distraction_count) + ",";
    data += "\"reminder_count\":" + std::to_string(snap.session.reminder_count);
    data += "},";
    data += "\"device\":{";
    data += "\"battery_percent\":" + std::to_string(snap.device.battery_percent) + ",";
    data += "\"system_mode\":" + std::to_string(static_cast<int>(snap.device.system_mode)) + ",";
    data += "\"camera_ok\":" + std::string(snap.device.camera_ok ? "true" : "false") + ",";
    data += "\"bracelet_connected\":" + std::string(snap.device.bracelet_connected ? "true" : "false");
    data += "},";
    data += "\"reminder\":{";
    data += "\"level\":" + std::to_string(static_cast<int>(snap.reminder.level)) + ",";
    data += "\"message\":\"" + jsonEscape(snap.reminder.message) + "\",";
    data += "\"since_ms\":" + std::to_string(snap.reminder.since_ms);
    data += "},";
    data += "\"frame_quality\":{";
    data += "\"score\":" + std::to_string(snap.frame_quality.score) + ",";
    data += "\"reason\":\"" + jsonEscape(snap.frame_quality.reason) + "\",";
    data += "\"guidance_needed\":" + std::string(snap.frame_quality.guidance_needed ? "true" : "false");
    data += "},";
    data += "\"today\":{";
    data += "\"total_seconds\":" + std::to_string(snap.today.total_seconds) + ",";
    data += "\"focus_seconds\":" + std::to_string(snap.today.focus_seconds) + ",";
    data += "\"sessions\":" + std::to_string(snap.today.sessions) + ",";
    data += "\"events\":" + std::to_string(snap.today.events);
    data += "}";
    data += "}";
    return jsonResponse(true, data);
}

// ---------------------------------------------------------------------------
// Push events
// ---------------------------------------------------------------------------

void BleGattServer::pushEvent(const std::string& event_type, const std::string& json_data) {
    if (!phone_connected_) return;
    // On Linux, this would write to RX characteristic via D-Bus Notify
    LOGI("BleGattServer", "Push event: %s", event_type.c_str());
    // push_notification(event_type, json_data);
}

} // namespace edgefocus
