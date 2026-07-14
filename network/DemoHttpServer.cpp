#include "DemoHttpServer.h"

#include "app/AppContext.h"
#include "app/Logger.h"
#include "behavior/PhoneFactBuffer.h"
#include "camera/PreviewFrameStore.h"
#include "network/PhoneSyncParser.h"
#include "policy/NotificationRouter.h"
#include "session/SessionOrchestrator.h"
#include "session/SessionSnapshotPublisher.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>

#ifdef EDGEFOCUS_WITH_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

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

const char* powerProfileWire(PowerProfile profile) {
    return profile == PowerProfile::HOME ? "home" : "go";
}

int displayBatteryPercent(const DeviceStatus& device) {
    return device.battery_percent >= 0 && device.battery_percent <= 100 ? device.battery_percent : 67;
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
    case 503: status_text = "Service Unavailable"; break;
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
<h3>Power State</h3>
<button onclick="power(true)">External Power / Charging</button>
<button onclick="power(false)">Battery / GO</button>
<input id="bat" type="number" value="67" min="0" max="100">Battery %
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
function power(external){cmd('set_power_state',{external_power:external,battery_charging:external,battery_percent:+bat.value})}
snapshot()
</script>
</body>
</html>)HTML";
}

std::string runtimeDemoPageHtml() {
    return R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EdgeFocus 会话推理链</title><style>
:root{--bg:#071018;--panel:#0b1621;--line:#203343;--ink:#e7f3f4;--muted:#78909d;--cyan:#49d6ff;--green:#5ee6a8;--amber:#ffca61;--red:#ff625f}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 15% 0,#102737 0,transparent 38%),var(--bg);color:var(--ink);font-family:"Microsoft YaHei UI",sans-serif;min-height:100vh}
body:before{content:"";position:fixed;inset:0;pointer-events:none;opacity:.18;background-image:linear-gradient(#5f82920d 1px,transparent 1px),linear-gradient(90deg,#5f82920d 1px,transparent 1px);background-size:24px 24px}
header{height:68px;padding:0 22px;border-bottom:1px solid var(--line);display:flex;align-items:center;justify-content:space-between;background:#071018dc;backdrop-filter:blur(10px)}
.brand{display:flex;gap:13px;align-items:center}.mark{width:27px;height:34px;display:flex;align-items:end;gap:3px}.mark i{display:block;width:7px;background:var(--cyan)}.mark i:nth-child(1){height:15px}.mark i:nth-child(2){height:30px}.mark i:nth-child(3){height:22px}.eyebrow,.label{font:700 9px/1.2 "Consolas",monospace;letter-spacing:2px;color:var(--cyan);text-transform:uppercase}.brand h1{font:600 22px/1 "Bahnschrift Condensed","Microsoft YaHei UI",sans-serif;margin:4px 0 0}.brand h1 span{font-weight:300;color:#91a5ae}
.metrics{display:flex;align-items:center;gap:24px}.metric{border-left:1px solid var(--line);padding-left:16px}.metric small{display:block;font:9px Consolas;color:var(--muted)}.metric b{font:700 13px Consolas;color:var(--ink)}.badge{padding:7px 11px;border:1px solid #285c48;color:var(--green);font:700 10px Consolas}.badge.warn{border-color:#694d26;color:var(--amber)}
main{position:relative;display:grid;grid-template-columns:minmax(0,1.8fr) minmax(330px,.8fr);gap:10px;padding:10px;min-height:calc(100vh - 68px)}.panel{background:#0a141eb8;border:1px solid var(--line)}
.stage{padding:10px}.panel-head{height:34px;display:flex;align-items:center;justify-content:space-between;font:700 10px Consolas;letter-spacing:1px;color:#9ab0bb}.index{color:var(--cyan);margin-right:10px}.source{color:var(--green)}
.video{position:relative;min-height:520px;height:calc(100vh - 184px);background:#020609;display:grid;place-items:center;overflow:hidden;border:1px solid #263b49}.video img{width:100%;height:100%;object-fit:contain}.empty{position:absolute;color:#526977;font:12px Consolas;letter-spacing:1px}.scan{position:absolute;inset:0;background:linear-gradient(transparent 49.7%,#49d6ff22 50%,transparent 50.3%);background-size:100% 9px;pointer-events:none}.hud{position:absolute;left:14px;top:13px;font:700 12px Consolas;text-shadow:0 1px 4px #000}.hud b{color:var(--cyan)}.stamp{position:absolute;bottom:12px;left:12px;display:flex;gap:8px}.stamp span{background:#071018d9;border:1px solid var(--line);padding:5px 8px;font:9px Consolas;color:var(--green)}
.sources{height:52px;display:flex;align-items:center;gap:8px;padding:8px 0 0}.source-btn{border:1px solid var(--line);background:#0b1822;color:#8197a2;padding:8px 10px;font:10px Consolas}.source-btn.active{color:var(--green);border-color:#2d7b59}.source-btn[disabled]{opacity:.45}.source-note{margin-left:auto;color:#5f7682;font:9px Consolas}
aside{display:flex;flex-direction:column;gap:8px}.evidence{position:relative;min-height:122px;padding:17px 18px;border-top:2px solid var(--green)}.evidence.amber{border-color:var(--amber)}.evidence.red{border-color:var(--red);flex:1}.evidence .code{position:absolute;right:14px;top:12px;font:30px Georgia;color:#314755}.evidence p{margin:0 0 8px;font:9px Consolas;letter-spacing:1.5px;color:#78909d}.evidence h2{margin:0 0 7px;font:600 21px "Bahnschrift Condensed","Microsoft YaHei UI"}.evidence small{color:#718893}.score{position:absolute;right:17px;bottom:15px;font:700 16px Consolas;color:var(--green)}
.phone-buttons,.session-buttons{display:flex;gap:5px;flex-wrap:wrap;margin-top:10px}button{cursor:pointer;border:1px solid #315063;background:#0b1721;color:#b5c7cd;padding:7px 10px;font:10px "Microsoft YaHei UI"}button:hover{border-color:var(--cyan);color:#fff}button.selected{border-color:var(--green);color:var(--green);background:#0c241c}.level{display:flex;gap:15px;align-items:center}.orb{width:55px;height:55px;border:1px solid #365064;display:grid;place-items:center;font:700 30px Consolas;color:var(--red)}.reason{font:10px Consolas;color:#9aaeb5;word-break:break-all}.footerline{font:9px Consolas;color:#5f7580;margin-top:auto;padding-top:10px;border-top:1px solid #1c303d}
@media(max-width:920px){main{grid-template-columns:1fr}.video{height:52vh;min-height:320px}header{height:auto;padding:12px}.metrics{gap:8px}.metric{display:none}}
</style></head><body>
<header><div class="brand"><div class="mark"><i></i><i></i><i></i></div><div><div class="eyebrow">EDGE AI · SESSION RUNTIME</div><h1>EdgeFocus <span>视觉推理链</span></h1></div></div><div class="metrics"><div class="metric"><small>PREVIEW</small><b id="preview-fps">-- FPS</b></div><div class="metric"><small>NPU INFERENCE</small><b id="npu-metric">-- FPS · -- ms</b></div><div id="health" class="badge warn">IDLE</div></div></header>
<main><section class="panel stage"><div class="panel-head"><span><b class="index">01</b>单一相机 · 真实推理输入</span><span id="source" class="source">LIVE CAMERA</span></div><div class="video"><div id="empty" class="empty">等待会话启动与服务端画面</div><img id="frame" alt="板端共享相机预览"><div class="scan"></div><div class="hud">SESSION <b id="session-id">--</b><br>FRAME <b id="frame-id">--</b></div><div class="stamp"><span id="phase">IDLE</span><span id="frame-age">NO EVIDENCE</span></div></div><div class="sources"><button class="source-btn active">现场摄像头 · REAL INFERENCE</button><button class="source-btn" disabled>预录视频 · 待放入</button><button class="source-btn" disabled>证据回放 · NPU故障备用</button><span class="source-note">回放状态永远不会伪装成 LIVE</span></div></section>
<aside><section class="panel evidence"><span class="code">A</span><p>VISUAL EVIDENCE · PRESENCE</p><h2 id="presence">等待证据</h2><small id="presence-detail">会话外不运行 NPU</small><b id="presence-score" class="score">--</b></section>
<section class="panel evidence amber"><span class="code">B</span><p>TEMPORAL MLP · PRONE</p><h2 id="prone">等待时序窗口</h2><small>单帧关键点不会直接触发趴下结论</small><b id="prone-score" class="score">--</b></section>
<section class="panel evidence"><p>APP RAW FACT · PHONE</p><h2 id="phone">未使用</h2><div class="phone-buttons"><button data-phone="OFF">未使用</button><button data-phone="LEARNING">学习</button><button data-phone="ENTERTAINMENT">娱乐</button><button data-phone="GENERAL_OR_UNKNOWN">其他</button></div></section>
<section class="panel evidence red"><p>FUSED DISTRACTION · SESSION OUTPUT</p><div class="level"><div id="level" class="orb">—</div><div><h2 id="level-title">当前无分神等级</h2><div id="reasons" class="reason">UNKNOWN</div></div></div><div class="session-buttons"><button data-action="start">开始会话</button><button data-action="confirm">通过预检</button><button data-action="pause">暂停</button><button data-action="resume">恢复</button><button data-action="end">结束</button></div><div id="gimbal" class="footerline">GIMBAL · IDLE</div></section></aside></main>
<script>
const $=id=>document.getElementById(id);let previewFrames=0,previewAt=performance.now(),lastSeq=-1;
async function json(url,opt){const r=await fetch(url,opt);const x=await r.json();if(!r.ok)throw Error(x.error||r.status);return x}
const pct=v=>v==null?'--':Math.round(v*100)+'%';
function render(s){$('session-id').textContent=(s.session.id||'--').slice(-12);$('phase').textContent=s.session.phase;$('source').textContent=s.source.label;$('frame-id').textContent=s.visual.frame_id||'--';$('npu-metric').textContent=`${Number(s.health.inference_fps||0).toFixed(1)} FPS · ${Number(s.health.inference_latency_ms||0).toFixed(0)} ms`;$('health').textContent=s.health.status;$('health').className='badge '+(s.health.status==='RUNNING'?'':'warn');
const p=s.visual.presence;$('presence').textContent=p==null?'证据不足':p>=.5?'人在':'人不在';$('presence-score').textContent=pct(p);$('presence-detail').textContent=s.assessment.applicability==='NOT_PRESENT'?'不输出分神等级':`证据帧新鲜度 ${s.health.frame_age_ms} ms`;
const q=s.visual.prone;$('prone').textContent=q==null?'等待时序窗口':q>=.5?'已确认趴下':'未趴下';$('prone-score').textContent=pct(q);$('phone').textContent=s.phone.active?({LEARNING:'学习类使用',ENTERTAINMENT:'娱乐类使用',GENERAL_OR_UNKNOWN:'其他使用'}[s.phone.category]||'使用中'):'未使用';document.querySelectorAll('[data-phone]').forEach(b=>b.classList.toggle('selected',(s.phone.active?b.dataset.phone===s.phone.category:b.dataset.phone==='OFF')));
const l=s.assessment.level;$('level').textContent=l==null?'—':l;$('level-title').textContent=l==null?(s.assessment.applicability==='NOT_PRESENT'?'人不在 · 等级不适用':'当前无分神等级'):["未分神","轻度分神","持续分神","严重 / 长时分神"][l];$('reasons').textContent=(s.assessment.reasons||[]).join(' + ')||s.assessment.applicability;$('gimbal').textContent=`GIMBAL · ${s.gimbal.state} · ${s.gimbal.health}`;
if(s.preview.available&&s.preview.sequence!==lastSeq){lastSeq=s.preview.sequence;$('frame').src=`/api/demo/frame.jpg?sequence=${lastSeq}&t=${Date.now()}`;$('empty').style.display='none'}const now=Date.now();$('frame-age').textContent=s.health.frame_age_ms>=0?`EVIDENCE ${s.health.frame_age_ms} ms OLD`:'NO EVIDENCE'}
$('frame').addEventListener('load',()=>{previewFrames++;const now=performance.now();if(now-previewAt>1000){$('preview-fps').textContent=(previewFrames*1000/(now-previewAt)).toFixed(1)+' FPS';previewFrames=0;previewAt=now}});
document.querySelectorAll('[data-phone]').forEach(b=>b.onclick=()=>json('/api/demo/phone',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({category:b.dataset.phone})}));document.querySelectorAll('[data-action]').forEach(b=>b.onclick=()=>json('/api/demo/session',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({action:b.dataset.action,target_duration_min:25})}));
async function poll(){try{render(await json('/api/demo/state'))}catch(e){$('health').textContent='OFFLINE';$('health').className='badge warn'}}poll();setInterval(poll,180);
</script></body></html>)HTML";
}
} // namespace

DemoHttpServer::DemoHttpServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                               DeviceManager& device_mgr, GimbalMonitor& gimbal_monitor,
                               session::SessionOrchestrator& sessions,
                               session::SessionSnapshotPublisher& snapshots,
                               PhoneFactBuffer& phone_facts,
                               PreviewFrameStore& preview_frames,
                               int port)
    : Module("DemoHttpServer")
    , store_(store)
    , sqlite_store_(sqlite_store)
    , device_mgr_(device_mgr)
    , gimbal_monitor_(gimbal_monitor)
    , sessions_(sessions)
    , snapshots_(snapshots)
    , phone_facts_(phone_facts)
    , preview_frames_(preview_frames)
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
            return httpResponse(200, runtimeDemoPageHtml(), "text/html; charset=utf-8");
        }
        if (method == "GET" && path == "/api/demo/state") {
            return httpResponse(200, runtimeSnapshotJson().dump());
        }
        if (method == "GET" && path.rfind("/api/demo/frame.jpg", 0) == 0) {
            return previewFrameResponse();
        }
        if (method == "POST" && path == "/api/demo/session") {
            const json data = body.empty() ? json::object() : json::parse(body);
            return httpResponse(200, handleRuntimeSession(data).dump());
        }
        if (method == "POST" && path == "/api/demo/phone") {
            const json data = body.empty() ? json::object() : json::parse(body);
            const auto response = handleRuntimePhone(data);
            return httpResponse(response.value("accepted", false) ? 200 : 400, response.dump());
        }
        // Legacy mutation endpoints are intentionally no longer routed. Demo
        // state can only come from SessionSnapshotPublisher.
        return httpResponse(404, json{{"status", "error"}, {"error", "unknown endpoint"}}.dump());
    } catch (const std::exception& e) {
        return httpResponse(400, json{{"status", "error"}, {"error", e.what()}}.dump());
    }
}

DemoHttpServer::json DemoHttpServer::runtimeSnapshotJson() const {
    const auto runtime = snapshots_.read();
    const auto legacy = store_.snapshot();
    const auto preview = preview_frames_.read();
    const auto now_ms = nowMs();

    auto optionalNumber = [](const std::optional<double>& value) -> json {
        return value ? json(*value) : json(nullptr);
    };
    auto category = [](const std::optional<session::PhoneCategory>& value) -> json {
        if (!value) return nullptr;
        switch (*value) {
            case session::PhoneCategory::Learning: return "LEARNING";
            case session::PhoneCategory::Entertainment: return "ENTERTAINMENT";
            case session::PhoneCategory::GeneralOrUnknown: return "GENERAL_OR_UNKNOWN";
        }
        return nullptr;
    };
    auto applicability = [](session::Applicability value) {
        if (value == session::Applicability::Active) return "ACTIVE";
        if (value == session::Applicability::NotPresent) return "NOT_PRESENT";
        return "UNKNOWN";
    };
    auto source = [](session::SourceMode value) {
        if (value == session::SourceMode::RecordedInput) return "RECORDED INPUT · REAL INFERENCE";
        if (value == session::SourceMode::EvidenceReplay) return "EVIDENCE REPLAY · NOT LIVE";
        return "LIVE CAMERA · REAL INFERENCE";
    };

    const bool evidence_available = runtime.visual.evidence_timestamp_ms > 0;
    const auto frame_age_ms = evidence_available
                                  ? std::max<std::int64_t>(0, now_ms - runtime.visual.evidence_timestamp_ms)
                                  : -1;
    const bool running = runtime.runtime_active && legacy.device.camera_ok;
    return {
        {"session", {
            {"id", runtime.context.session_id},
            {"phase", session::phase_name(runtime.phase)},
            {"active", runtime.runtime_active},
            {"elapsed_ms", runtime.elapsed_ms},
            {"remaining_ms", runtime.remaining_ms}
        }},
        {"source", {{"mode", static_cast<int>(runtime.context.source_mode)},
                    {"label", source(runtime.context.source_mode)}}},
        {"health", {
            {"status", running ? "RUNNING" : (runtime.runtime_active ? "STARTING" : "IDLE")},
            {"camera", legacy.device.camera_ok},
            {"inference_fps", runtime.visual.inference_fps},
            {"inference_latency_ms", runtime.visual.inference_latency_ms},
            {"frame_age_ms", frame_age_ms}
        }},
        {"visual", {
            {"presence", optionalNumber(runtime.visual.presence)},
            {"prone", optionalNumber(runtime.visual.prone)},
            {"frame_id", runtime.visual.frame_id}
        }},
        {"phone", {
            {"fresh", runtime.phone.fresh},
            {"active", runtime.phone.active},
            {"category", category(runtime.phone.category)}
        }},
        {"assessment", {
            {"applicability", applicability(runtime.assessment.applicability)},
            {"level", runtime.assessment.level ? json(*runtime.assessment.level) : json(nullptr)},
            {"reasons", runtime.assessment.reasons},
            {"would_remind", runtime.assessment.would_remind}
        }},
        {"gimbal", {
            {"state", runtime.gimbal.state},
            {"health", runtime.gimbal.health.status},
            {"tracking", runtime.gimbal.tracking}
        }},
        {"preview", {
            {"available", preview.has_value()},
            {"sequence", preview ? preview->sequence : 0}
        }},
        {"backup_sources", json::array({
            {{"mode", "RECORDED_INPUT"}, {"ready", false}, {"label", "预录视频 · 待放入"}},
            {{"mode", "EVIDENCE_REPLAY"}, {"ready", false}, {"label", "证据回放 · 待冻结"}}
        })}
    };
}

DemoHttpServer::json DemoHttpServer::handleRuntimeSession(const json& data) {
    const std::string action = lowerCopy(data.value("action", ""));
    session::SessionOrchestrator::Result result;
    if (action == "start") {
        const int minutes = clampInt(data.value("target_duration_min", 25), 5, 120);
        result = sessions_.start({static_cast<std::int64_t>(minutes) * 60000,
                                  session::SourceMode::LiveCamera});
    } else if (action == "confirm") {
        result = sessions_.confirmReady();
    } else if (action == "pause") {
        result = sessions_.pause();
    } else if (action == "resume") {
        result = sessions_.resume();
    } else if (action == "end") {
        result = sessions_.end(session::StopReason::Completed);
    } else {
        return {{"success", false}, {"error", "unknown session action"}};
    }
    return {{"success", result.success},
            {"error", result.error},
            {"phase", session::phase_name(sessions_.phase())}};
}

DemoHttpServer::json DemoHttpServer::handleRuntimePhone(const json& data) {
    const auto runtime = snapshots_.read();
    if (!runtime.runtime_active || runtime.context.session_id.empty()) {
        return {{"accepted", false}, {"error", "start a session before publishing phone facts"}};
    }
    const std::string category_text = data.value("category", "OFF");
    session::PhoneFact fact;
    fact.session_id = runtime.context.session_id;
    fact.sequence = ++phone_sequence_;
    fact.received_monotonic_ms = nowMs();
    fact.source_timestamp_ms = fact.received_monotonic_ms;
    fact.fresh_for_ms = 2000;
    fact.active = category_text != "OFF";
    if (category_text == "LEARNING") {
        fact.category = session::PhoneCategory::Learning;
    } else if (category_text == "ENTERTAINMENT") {
        fact.category = session::PhoneCategory::Entertainment;
    } else if (category_text == "GENERAL_OR_UNKNOWN") {
        fact.category = session::PhoneCategory::GeneralOrUnknown;
    } else if (category_text != "OFF") {
        return {{"accepted", false}, {"error", "invalid phone category"}};
    }
    std::string error;
    const bool accepted = phone_facts_.publish(fact, &error);
    return {{"accepted", accepted}, {"sequence", fact.sequence}, {"error", error}};
}

std::string DemoHttpServer::previewFrameResponse() const {
    const auto preview = preview_frames_.read();
    if (!preview) {
        return httpResponse(503, json{{"error", "preview frame unavailable"}}.dump());
    }
#ifdef EDGEFOCUS_WITH_OPENCV
    const auto& frame = preview->frame;
    cv::Mat image;
    if (frame.channels == 1 && frame.format == PixelFormat::GRAY8) {
        cv::Mat gray(frame.height, frame.width, CV_8UC1,
                     const_cast<std::uint8_t*>(frame.data.data()));
        cv::cvtColor(gray, image, cv::COLOR_GRAY2BGR);
    } else if (frame.channels == 3) {
        cv::Mat packed(frame.height, frame.width, CV_8UC3,
                       const_cast<std::uint8_t*>(frame.data.data()));
        if (frame.format == PixelFormat::RGB24) {
            cv::cvtColor(packed, image, cv::COLOR_RGB2BGR);
        } else {
            image = packed.clone();
        }
    } else {
        return httpResponse(503, json{{"error", "unsupported preview format"}}.dump());
    }
    const auto runtime = snapshots_.read();
    const std::string overlay = std::string("SERVER CAMERA  |  NPU ") +
                                std::to_string(runtime.visual.inference_fps).substr(0, 3) + " FPS";
    cv::putText(image, overlay, {18, 30}, cv::FONT_HERSHEY_SIMPLEX, 0.62,
                cv::Scalar(73, 214, 255), 2, cv::LINE_AA);
    std::vector<std::uint8_t> jpeg;
    if (!cv::imencode(".jpg", image, jpeg, {cv::IMWRITE_JPEG_QUALITY, 82})) {
        return httpResponse(503, json{{"error", "jpeg encoding failed"}}.dump());
    }
    return httpResponse(200,
                        std::string(reinterpret_cast<const char*>(jpeg.data()), jpeg.size()),
                        "image/jpeg");
#else
    return httpResponse(503, json{{"error", "OpenCV preview not compiled"}}.dump());
#endif
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
            {"battery_percent", displayBatteryPercent(snap.device)},
            {"battery_charging", snap.device.battery_charging},
            {"power_source", snap.device.power_source},
            {"power_profile", powerProfileWire(snap.device.power_profile)},
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
    if (cmd == "set_power_state") return setPowerState(data);
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

DemoHttpServer::json DemoHttpServer::setPowerState(const json& data) {
    auto snap = store_.snapshot();
    DeviceStatus device = snap.device;
    const bool external_power = data.value(
        "external_power",
        data.value("battery_charging", device.battery_charging));
    device.battery_charging = data.value("battery_charging", external_power);
    device.power_source = external_power ? "external" : "battery";
    device.power_profile = external_power ? PowerProfile::HOME : PowerProfile::GO;
    device.battery_percent = clampInt(
        data.value("battery_percent", device.battery_percent >= 0 ? device.battery_percent : 67),
        0,
        100);
    store_.updateDeviceStatus(device);
    return json{{"status", "success"},
                {"data", {
                    {"battery_percent", device.battery_percent},
                    {"battery_charging", device.battery_charging},
                    {"power_source", device.power_source},
                    {"power_profile", powerProfileWire(device.power_profile)}
                }}};
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
