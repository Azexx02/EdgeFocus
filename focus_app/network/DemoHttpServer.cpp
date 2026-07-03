#include "DemoHttpServer.h"

#include "app/AppContext.h"
#include "app/Logger.h"
#include "network/PhoneSyncParser.h"
#include "policy/NotificationRouter.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>

#ifdef __linux__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace edgefocus {

using json = nlohmann::json;

namespace {
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int clampInt(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

FocusState parseFocusState(const json& value) {
    if (value.is_number_integer()) {
        return static_cast<FocusState>(value.get<int>());
    }
    const auto text = lowerCopy(value.get<std::string>());
    if (text == "focused") return FocusState::FOCUSED;
    if (text == "looking_away") return FocusState::LOOKING_AWAY;
    if (text == "sleepy_or_slumped") return FocusState::SLEEPY_OR_SLUMPED;
    if (text == "phone_in_use") return FocusState::PHONE_IN_USE;
    if (text == "away") return FocusState::AWAY;
    if (text == "low_confidence") return FocusState::LOW_CONFIDENCE;
    if (text == "phone_in_learning") return FocusState::PHONE_IN_LEARNING;
    if (text == "phone_in_playing") return FocusState::PHONE_IN_PLAYING;
    return FocusState::FOCUSED;
}

CommandLevel parseCommandLevel(int level) {
    if (level <= 1) return CommandLevel::LEVEL_1;
    if (level == 2) return CommandLevel::LEVEL_2;
    return CommandLevel::LEVEL_3;
}

std::string httpResponse(int status, const std::string& body,
                         const std::string& content_type = "application/json") {
    const char* status_text = "Bad Request";
    switch (status) {
    case 200: status_text = "OK"; break;
    case 400: status_text = "Bad Request"; break;
    case 404: status_text = "Not Found"; break;
    case 413: status_text = "Payload Too Large"; break;
    case 500: status_text = "Internal Server Error"; break;
    default: break;
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << status_text << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Headers: content-type\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

std::string demoPageHtml() {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EdgeFocus Demo Console</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:20px;background:#f6f7f9;color:#111}
h1{font-size:22px;margin:0 0 16px}
p{margin:0 0 12px;color:#444}
section{background:#fff;border:1px solid #ddd;border-radius:8px;padding:14px;margin:12px 0}
button{margin:4px;padding:8px 10px;border:1px solid #bbb;border-radius:6px;background:#fff}
button.primary{background:#0b57d0;color:#fff;border-color:#0b57d0}
input,select{margin:4px;padding:7px;border:1px solid #bbb;border-radius:6px}
pre{white-space:pre-wrap;background:#111;color:#eee;padding:12px;border-radius:8px;min-height:120px}
</style>
</head>
<body>
<h1>EdgeFocus Demo Console</h1>
<p>Video-only demo page. Open it from another machine with the board host IP and the configured port.</p>
<section>
<button class="primary" onclick="snapshot()">Refresh Snapshot</button>
<button onclick="cmd('clear_overrides')">Clear Overrides</button>
</section>
<section>
<h3>Focus Override</h3>
<button onclick="forceState('FOCUSED',10)">FOCUSED</button>
<button onclick="forceState('LOOKING_AWAY',80)">LOOKING_AWAY</button>
<button onclick="forceState('AWAY',85)">AWAY</button>
<button onclick="forceState('PHONE_IN_USE',70)">PHONE_IN_USE</button>
<button onclick="forceState('PHONE_IN_PLAYING',95)">PHONE_IN_PLAYING</button>
</section>
<section>
<h3>Precheck Guidance</h3>
<button onclick="precheck('move_closer','sit_in_front_of_camera',false)">Move Closer</button>
<button onclick="precheck('move_farther_or_center','keep_head_in_frame',false)">Move Back or Center</button>
<button onclick="precheck('distance_ok','pose_ok',true)">Ready</button>
</section>
<section>
<h3>Reminder</h3>
<button onclick="cmd('trigger_reminder',{level:1,message:'Stay focused'})">Trigger L1</button>
<button onclick="cmd('trigger_reminder',{level:2,message:'Return to study'})">Trigger L2</button>
<button onclick="cmd('trigger_reminder',{level:3,message:'Put the phone down'})">Trigger L3</button>
</section>
<section>
<h3>Gimbal Nudge</h3>
<button onclick="nudge('up')">Up</button>
<button onclick="nudge('left')">Left</button>
<button onclick="nudge('right')">Right</button>
<button onclick="nudge('down')">Down</button>
</section>
<section>
<h3>Report Counters</h3>
<input id="d" type="number" value="2" min="0">Distractions
<input id="r" type="number" value="2" min="0">Reminders
<input id="p" type="number" value="1" min="0">Phone events
<button onclick="counts()">Apply Counts</button>
</section>
<pre id="out"></pre>
<script>
async function post(body){const r=await fetch('/demo/command',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(body)});show(await r.json())}
function show(x){document.getElementById('out').textContent=JSON.stringify(x,null,2)}
async function snapshot(){const r=await fetch('/demo/snapshot');show(await r.json())}
function cmd(c,data={}){post({cmd:c,data})}
function forceState(s,risk){cmd('force_focus_state',{focus_state:s,risk_score:risk,reason:'demo_console'})}
function precheck(d,p,ready){cmd('force_precheck_guidance',{distance_hint:d,pose_hint:p,ready})}
function nudge(direction){cmd('gimbal_nudge',{direction,step_deg:3})}
function counts(){cmd('set_demo_counts',{distraction_count:+d.value,reminder_count:+r.value,phone_distraction_count:+p.value})}
snapshot()
</script>
</body>
</html>)HTML";
}
} // namespace

DemoHttpServer::DemoHttpServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                               DeviceManager& device_mgr, GimbalMonitor& gimbal_monitor,
                               int port)
    : Module("DemoHttpServer")
    , store_(store)
    , sqlite_store_(sqlite_store)
    , device_mgr_(device_mgr)
    , gimbal_monitor_(gimbal_monitor)
    , port_(port) {}

bool DemoHttpServer::init(AppContext& /*ctx*/) {
    if (port_ <= 0) return true;
#ifndef __linux__
    LOGW("DemoHttpServer", "Demo HTTP is only enabled on Linux builds");
    return true;
#else
    running_ = true;
    server_thread_ = std::thread(&DemoHttpServer::serveLoop, this);
    return true;
#endif
}

void DemoHttpServer::runOnce(AppContext& /*ctx*/) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

void DemoHttpServer::shutdown(AppContext& /*ctx*/) {
    running_ = false;
    if (server_thread_.joinable()) {
#ifdef __linux__
        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(port_));
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            ::close(sock);
        }
#endif
        server_thread_.join();
    }
}

void DemoHttpServer::serveLoop() {
#ifndef __linux__
    return;
#else
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOGE("DemoHttpServer", "socket failed");
        return;
    }
    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(server_fd, 4) < 0) {
        LOGE("DemoHttpServer", "bind/listen failed on port %d", port_);
        ::close(server_fd);
        return;
    }
    LOGI("DemoHttpServer", "Demo HTTP listening on 0.0.0.0:%d", port_);

    while (running_) {
        int client = ::accept(server_fd, nullptr, nullptr);
        if (client < 0) continue;
        timeval io_timeout{};
        io_timeout.tv_sec = 2;
        io_timeout.tv_usec = 0;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &io_timeout, sizeof(io_timeout));
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &io_timeout, sizeof(io_timeout));

        std::string request_buffer;
        request_buffer.reserve(4096);
        std::string response;
        bool have_response = false;

        while (running_ && !have_response) {
            char buffer[4096];
            const ssize_t n = ::recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                if (!request_buffer.empty()) {
                    response = httpResponse(
                        400, json{{"status", "error"}, {"error", "unexpected end of request"}}.dump());
                    have_response = true;
                }
                break;
            }

            request_buffer.append(buffer, static_cast<size_t>(n));
            demo_http::DemoHttpParsedRequest request;
            std::string parse_error;
            size_t expected_bytes = 0;
            const auto parse_status = demo_http::parseDemoHttpRequestBuffer(
                request_buffer, request, expected_bytes, parse_error);
            if (parse_status == demo_http::DemoHttpParseStatus::INCOMPLETE) {
                continue;
            }
            if (parse_status == demo_http::DemoHttpParseStatus::ERROR) {
                const int status = parse_error == "request body too large" ? 413 : 400;
                response = httpResponse(status, json{{"status", "error"}, {"error", parse_error}}.dump());
                have_response = true;
                break;
            }

            response = handleRequest(request.method, request.path, request.body);
            have_response = true;
        }

        if (have_response) {
            size_t sent_total = 0;
            while (sent_total < response.size()) {
                const ssize_t sent = ::send(client, response.data() + sent_total,
                                            response.size() - sent_total, 0);
                if (sent <= 0) {
                    break;
                }
                sent_total += static_cast<size_t>(sent);
            }
        }
        ::close(client);
    }

    ::close(server_fd);
#endif
}

std::string DemoHttpServer::handleRequest(const std::string& method, const std::string& path,
                                          const std::string& body) {
    try {
        if (method == "OPTIONS") return httpResponse(200, "{}");
        if (method == "GET" && (path == "/" || path == "/demo")) {
            return httpResponse(200, demoPageHtml(), "text/html; charset=utf-8");
        }
        if (method == "GET" && path == "/demo/snapshot") {
            return httpResponse(200, snapshotJson().dump());
        }
        if (method == "POST" && path == "/demo/command") {
            json req = body.empty() ? json::object() : json::parse(body);
            return httpResponse(200, handleCommand(req).dump());
        }
        return httpResponse(404, json{{"status", "error"}, {"error", "unknown endpoint"}}.dump());
    } catch (const std::exception& e) {
        return httpResponse(400, json{{"status", "error"}, {"error", e.what()}}.dump());
    }
}

DemoHttpServer::json DemoHttpServer::snapshotJson() const {
    auto snap = store_.snapshot();
    return {
        {"status", "success"},
        {"data", {
            {"session_id", snap.session.session_id},
            {"session_active", snap.session.active},
            {"focus_state", static_cast<int>(snap.focus_state)},
            {"focus_reason", snap.focus_reason},
            {"distraction_count", snap.session.distraction_count},
            {"reminder_count", snap.session.reminder_count},
            {"phone_distraction_count", snap.session.phone_distraction_count},
            {"risk_score", snap.behavior_context.risk_score},
            {"gimbal_enabled", snap.preferences.gimbal_enabled},
            {"demo_focus_override", snap.demo_override.focus_override_enabled},
            {"demo_precheck_override", snap.demo_override.precheck_guidance_override_enabled},
            {"gimbal", {
                {"connected", snap.device.gimbal_connected},
                {"online", snap.device.gimbal_online},
                {"safe", snap.device.gimbal_safe},
                {"angle_x", snap.device.gimbal_angle_x},
                {"angle_y", snap.device.gimbal_angle_y},
                {"last_command_status", snap.device.gimbal_last_command_status}
            }}
        }}
    };
}

DemoHttpServer::json DemoHttpServer::handleCommand(const json& req) {
    const auto cmd = req.value("cmd", "");
    const json data = req.contains("data") ? req["data"] : json::object();
    if (cmd == "get_demo_snapshot") return snapshotJson();
    if (cmd == "force_focus_state") return forceFocusState(data);
    if (cmd == "force_precheck_guidance") return forcePrecheckGuidance(data);
    if (cmd == "force_phone_state") return forcePhoneState(data);
    if (cmd == "trigger_reminder") return triggerReminder(data);
    if (cmd == "set_demo_counts") return setDemoCounts(data);
    if (cmd == "gimbal_nudge") return gimbalNudge(data);
    if (cmd == "clear_overrides") {
        store_.clearDemoOverride();
        return json{{"status", "success"}, {"data", {{"cleared", true}}}};
    }
    return json{{"status", "error"}, {"error", "unknown demo command"}};
}

DemoHttpServer::json DemoHttpServer::forceFocusState(const json& data) {
    DemoOverride demo = store_.snapshot().demo_override;
    demo.focus_override_enabled = true;
    demo.focus_state = parseFocusState(data.at("focus_state"));
    demo.risk_score = clampInt(data.value("risk_score", 80), 0, 100);
    demo.reason = data.value("reason", "demo_override");
    store_.updateDemoOverride(demo);
    return json{{"status", "success"}, {"data", {{"focus_state", static_cast<int>(demo.focus_state)}}}};
}

DemoHttpServer::json DemoHttpServer::forcePrecheckGuidance(const json& data) {
    DemoOverride demo = store_.snapshot().demo_override;
    demo.precheck_guidance_override_enabled = true;
    demo.distance_hint = data.value("distance_hint", "distance_ok");
    demo.pose_hint = data.value("pose_hint", "pose_ok");
    demo.precheck_ready = data.value("ready", true);
    store_.updateDemoOverride(demo);
    return json{{"status", "success"}, {"data", {{"distance_hint", demo.distance_hint},
                                                  {"pose_hint", demo.pose_hint},
                                                  {"ready", demo.precheck_ready}}}};
}

DemoHttpServer::json DemoHttpServer::forcePhoneState(const json& data) {
    PhoneUsageSignal sig;
    auto parsed = parsePhoneSyncPayload(data, false);
    sig.active = parsed.active;
    sig.distraction_level = parsed.distraction_level;
    sig.usage_state = parsed.usage_state;
    sig.timestamp_ms = nowMs();
    store_.setPhoneSignal(sig);
    return json{{"status", "success"}, {"data", {{"queued", true}}}};
}

DemoHttpServer::json DemoHttpServer::triggerReminder(const json& data) {
    const auto level = parseCommandLevel(clampInt(data.value("level", 1), 1, 3));
    const auto message = data.value("message", "demo reminder");
    auto prefs = store_.snapshot().preferences;
    json phone_actions = json::array();

    auto maybeIssue = [&](NotificationChannel channel) {
        if (channel == NotificationChannel::SCREEN && prefs.screen_output_route == "phone") {
            phone_actions.push_back("SCREEN");
            return;
        }
        if (channel == NotificationChannel::SOUND && prefs.sound_output_route == "phone") {
            phone_actions.push_back("SOUND");
            return;
        }
        device_mgr_.issueAction({channel, level});
    };

    const int mask = prefs.system_mode == static_cast<int>(SystemMode::SILENT)
                         ? prefs.silent_channels_mask
                         : prefs.normal_channels_mask;
    for (auto channel : notificationMaskToChannels(mask)) {
        maybeIssue(channel);
    }

    store_.updateReminder(level, message, nowMs());
    json push = {
        {"cmd", "push_alert"},
        {"data", {
            {"level", static_cast<int>(level)},
            {"message", message},
            {"reason", "demo_trigger"},
            {"phone_actions", phone_actions},
            {"sound_volume", prefs.sound_volume},
            {"focus_state", static_cast<int>(store_.snapshot().focus_state)},
            {"reminder_count", store_.snapshot().session.reminder_count}
        }}
    };
    store_.pushEvent(push.dump());
    return json{{"status", "success"}, {"data", {{"phone_actions", phone_actions}}}};
}

DemoHttpServer::json DemoHttpServer::setDemoCounts(const json& data) {
    SessionInfo session = store_.snapshot().session;
    if (data.contains("distraction_count")) {
        session.distraction_count = std::max(0, data["distraction_count"].get<int>());
    }
    if (data.contains("reminder_count")) {
        session.reminder_count = std::max(0, data["reminder_count"].get<int>());
    }
    if (data.contains("phone_distraction_count")) {
        session.phone_distraction_count = std::max(0, data["phone_distraction_count"].get<int>());
    }
    if (!session.active || !store_.updateSessionIfCurrent(session)) {
        return json{{"status", "error"}, {"error", "no active session"}};
    }
    return json{{"status", "success"}, {"data", {{"distraction_count", session.distraction_count},
                                                  {"reminder_count", session.reminder_count},
                                                  {"phone_distraction_count", session.phone_distraction_count}}}};
}

DemoHttpServer::json DemoHttpServer::gimbalNudge(const json& data) {
    if (!store_.snapshot().preferences.gimbal_enabled) {
        return json{{"status", "error"}, {"error", "gimbal disabled"}};
    }
    const auto direction = lowerCopy(data.value("direction", ""));
    const int step = clampInt(data.value("step_deg", 3), 1, 8);
    auto snap = store_.snapshot();
    if (!snap.device.gimbal_online || !snap.device.gimbal_safe) {
        return json{{"status", "error"}, {"error", "gimbal not ready for motion"}};
    }
    int x = clampInt(static_cast<int>(std::lround(snap.device.gimbal_angle_x)), 0, 180);
    int y = clampInt(static_cast<int>(std::lround(snap.device.gimbal_angle_y)), 0, 359);
    if (direction == "left") y = clampInt(y - step, 0, 359);
    else if (direction == "right") y = clampInt(y + step, 0, 359);
    else if (direction == "up") x = clampInt(x - step, 0, 180);
    else if (direction == "down") x = clampInt(x + step, 0, 180);
    else return json{{"status", "error"}, {"error", "direction must be left/right/up/down"}};

    auto result = gimbal_monitor_.executeBusinessCommand(
        "SET XY " + std::to_string(x) + " " + std::to_string(y), 15000);
    return json{{"status", "success"}, {"data", {{"success", result.success},
                                                  {"command", result.command},
                                                  {"final_response", result.final_response},
                                                  {"error", result.error}}}};
}

} // namespace edgefocus
