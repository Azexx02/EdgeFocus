#include "DemoHttpServer.h"
#include "demo/BoardDemoRuntime.h"

#include "app/AppContext.h"
#include "app/Logger.h"
#include "behavior/PhoneFactBuffer.h"
#include "camera/PreviewFrameStore.h"
#include "device/GimbalPrecheckController.h"
#include "network/PhoneSyncParser.h"
#include "policy/NotificationRouter.h"
#include "session/SessionOrchestrator.h"
#include "session/SessionSnapshotPublisher.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
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

#ifdef EDGEFOCUS_WITH_OPENCV
bool encodePreviewJpeg(const CameraFrame& frame, int quality,
                       std::vector<std::uint8_t>& jpeg) {
    if (!frame.valid()) return false;
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
            image = packed;
        }
    } else {
        return false;
    }
    jpeg.clear();
    return cv::imencode(".jpg", image, jpeg, {cv::IMWRITE_JPEG_QUALITY, quality});
}
#endif

#ifdef __linux__
bool sendNoSignal(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t sent_total = 0;
    while (sent_total < size) {
        const ssize_t sent = ::send(fd, bytes + sent_total, size - sent_total, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}
#endif

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

std::string runtimeDemoPageHtml() {
    return std::string(R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EdgeFocus 视觉推理链</title><link rel="icon" href="data:,"><style>
:root{--bg:#071018;--panel:#0b1621;--line:#203343;--ink:#e7f3f4;--muted:#78909d;--cyan:#49d6ff;--green:#5ee6a8;--amber:#ffca61;--red:#ff625f;--violet:#9f83ff}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 15% 0,#102737 0,transparent 38%),var(--bg);color:var(--ink);font-family:"Microsoft YaHei UI",sans-serif;min-height:100vh;overflow:auto}body:before{content:"";position:fixed;inset:0;pointer-events:none;opacity:.18;background-image:linear-gradient(#5f82920d 1px,transparent 1px),linear-gradient(90deg,#5f82920d 1px,transparent 1px);background-size:24px 24px}
header{height:68px;padding:0 22px;border-bottom:1px solid var(--line);display:flex;align-items:center;justify-content:space-between;background:#071018e8}.brand{display:flex;gap:13px;align-items:center}.mark{width:27px;height:34px;display:flex;align-items:end;gap:3px}.mark i{display:block;width:7px;background:var(--cyan)}.mark i:nth-child(1){height:15px}.mark i:nth-child(2){height:30px}.mark i:nth-child(3){height:22px}.eyebrow{font:700 9px/1.2 Consolas;letter-spacing:2px;color:var(--cyan)}.brand h1{font:600 22px/1 "Bahnschrift Condensed","Microsoft YaHei UI";margin:4px 0 0}.brand h1 span{font-weight:300;color:#91a5ae}.metrics{display:flex;align-items:center;gap:24px}.metric{border-left:1px solid var(--line);padding-left:16px}.metric small{display:block;font:9px Consolas;color:var(--muted)}.metric b{font:700 13px Consolas}.badge{padding:7px 11px;border:1px solid #285c48;color:var(--green);font:700 10px Consolas}.badge.warn{border-color:#694d26;color:var(--amber)}.badge.replay{border-color:#674c99;color:#bca8ff}
main{height:calc(100vh - 68px);display:grid;grid-template-columns:minmax(0,1.78fr) minmax(390px,.82fr);grid-template-rows:minmax(0,1fr) clamp(150px,22vh,232px);gap:9px;padding:9px}.panel{background:#0a141ed9;border:1px solid var(--line);min-width:0}.stage{padding:10px;display:flex;min-height:0;overflow:hidden;flex-direction:column}.panel-head{height:32px;display:flex;align-items:center;justify-content:space-between;font:700 10px Consolas;letter-spacing:1px;color:#9ab0bb}.index{color:var(--cyan);margin-right:10px}.source{color:var(--green)}
.video{position:relative;flex:1;min-height:0;background:#020609;display:grid;place-items:center;overflow:hidden;border:1px solid #263b49}.video img,.video video{display:none;position:absolute;max-width:none;max-height:none;object-fit:contain}.empty{position:absolute;color:#526977;font:12px Consolas;letter-spacing:1px}.scan{position:absolute;inset:0;background:linear-gradient(transparent 49.7%,#49d6ff18 50%,transparent 50.3%);background-size:100% 9px;pointer-events:none}.overlay{position:absolute;left:0;top:0;width:0;height:0;pointer-events:none}.hud{position:absolute;left:14px;top:13px;font:700 12px Consolas;text-shadow:0 1px 4px #000}.hud b{color:var(--cyan)}.stamp{position:absolute;bottom:12px;left:12px;display:flex;gap:8px}.stamp span{background:#071018e8;border:1px solid var(--line);padding:5px 8px;font:9px Consolas;color:var(--green)}
.toolbar{display:flex;align-items:center;gap:7px;padding-top:9px;min-height:48px}.mode-btn,.phone-buttons button{cursor:pointer;border:1px solid #315063;background:#0b1721;color:#9eb1ba;padding:7px 10px;font:10px "Microsoft YaHei UI"}.mode-btn:hover,.phone-buttons button:hover{border-color:var(--cyan);color:#fff}.mode-btn.active,.phone-buttons button.selected{border-color:var(--green);color:var(--green);background:#0c241c}.mode-btn[disabled],.phone-buttons button[disabled]{cursor:not-allowed;opacity:.38}.switch{display:flex;gap:6px;align-items:center;margin-left:8px;color:#718893;font:9px Consolas}.note{margin-left:auto;color:#5f7682;font:9px Consolas;text-align:right}
aside{display:grid;grid-template-columns:1fr 1fr;grid-template-rows:auto auto;gap:8px;min-height:0}.evidence{position:relative;padding:15px 16px;border-top:2px solid var(--green);min-height:0}.evidence.amber{border-color:var(--amber)}.evidence.violet{border-color:var(--violet)}.evidence.red{border-color:var(--red)}.evidence .code{position:absolute;right:12px;top:9px;font:25px Georgia;color:#314755}.evidence p{margin:0 0 7px;font:9px Consolas;letter-spacing:1.3px;color:#78909d}.evidence h2{margin:0 0 5px;font:600 20px "Bahnschrift Condensed","Microsoft YaHei UI"}.evidence small{font-size:10px;color:#718893}.score{position:absolute;right:14px;bottom:13px;font:700 14px Consolas;color:var(--green)}.phone-buttons{display:flex;gap:4px;flex-wrap:wrap;margin-top:9px}.phone-buttons button{padding:6px 8px}.level{display:flex;gap:12px;align-items:center}.orb{width:49px;height:49px;border:1px solid #365064;display:grid;place-items:center;font:700 27px Consolas;color:var(--red)}.reason{font:9px Consolas;color:#9aaeb5;word-break:break-all}.truth{margin-top:10px;padding-top:8px;border-top:1px solid #1c303d;font:9px Consolas;color:#607783}
.control-deck{grid-column:2;grid-row:2;display:grid;grid-template-rows:76px minmax(0,1fr);min-height:0}.session-control{padding:7px 11px;border-top:2px solid var(--green);border-bottom:1px solid var(--line)}.session-head,.gimbal-head{height:23px;display:flex;align-items:center;justify-content:space-between;font:700 10px Consolas;letter-spacing:1px;color:#9ab0bb}.session-clock,.gimbal-angle{color:var(--cyan)}.session-body{display:flex;align-items:center;gap:5px}.session-body input{width:49px;height:27px;margin:0;padding:4px 5px;border:1px solid #315063;background:#08131c;color:var(--ink);font:11px Consolas}.session-unit{font:9px Consolas;color:#718893;margin-right:3px}.session-actions{display:flex;gap:4px}.session-actions button{height:27px;cursor:pointer;border:1px solid #315063;background:#0b1721;color:#b8c9d0;padding:4px 7px;font:10px "Microsoft YaHei UI"}.session-actions button:hover{border-color:var(--cyan);color:#fff}.session-actions button[disabled]{cursor:not-allowed;opacity:.3}.session-actions .start{border-color:#2b7959;color:var(--green)}.session-actions .confirm{border-color:#34728a;color:var(--cyan)}.session-actions .end{border-color:#7d4a4a;color:#ff8d88}.session-detail{margin-left:auto;max-width:130px;text-align:right;color:#718893;font:9px Consolas;line-height:1.25}.gimbal-control{padding:7px 11px;display:flex;flex-direction:column;min-height:0}.gimbal-body{display:grid;grid-template-columns:98px minmax(0,1fr) auto;align-items:center;gap:12px;min-height:0;flex:1}.dpad{display:grid;grid-template-columns:repeat(3,28px);grid-template-rows:repeat(3,28px);gap:3px}.dpad button,.gimbal-actions button{cursor:pointer;border:1px solid #315063;background:#0b1721;color:#b8c9d0;font:700 13px Consolas}.dpad button:hover,.gimbal-actions button:hover{border-color:var(--cyan);color:#fff}.dpad button[disabled],.gimbal-actions button[disabled]{cursor:not-allowed;opacity:.3}.dpad .up{grid-column:2}.dpad .left{grid-column:1;grid-row:2}.dpad .center{grid-column:2;grid-row:2;color:var(--green)}.dpad .right{grid-column:3;grid-row:2}.dpad .down{grid-column:2;grid-row:3}.gimbal-info b{display:block;font:600 17px "Bahnschrift Condensed","Microsoft YaHei UI";margin-bottom:5px}.gimbal-info small{display:block;color:#718893;font:9px Consolas;line-height:1.45}.gimbal-actions{display:grid;gap:4px}.gimbal-actions button{min-width:80px;padding:5px 7px;font:10px "Microsoft YaHei UI"}.gimbal-actions .takeover{border-color:#786329;color:var(--amber)}.gimbal-actions .release{display:none;border-color:#2b7959;color:var(--green)}.gimbal-actions .release.show{display:block}.gimbal-actions .realign{display:none;border-color:#7d4a4a;color:#ff8d88}.gimbal-actions .realign.show{display:block}.gimbal-actions .force{display:none;border-color:#8f3232;color:#ff625f}.gimbal-actions .force.show{display:block}
.timeline{grid-column:1;grid-row:2;padding:8px 12px 10px;display:flex;flex-direction:column}.timeline .panel-head{height:27px}.legend{display:flex;gap:14px;font:9px Consolas}.legend i{display:inline-block;width:10px;height:2px;margin-right:5px;vertical-align:middle}.timeline canvas{width:100%;height:100px;display:block}
@media(max-height:700px) and (min-width:981px){header{height:54px}main{height:calc(100vh - 54px);grid-template-rows:minmax(0,1fr) 150px}.timeline canvas{height:70px}.control-deck{grid-template-rows:68px minmax(0,1fr)}.toolbar{min-height:38px}.panel-head{height:26px}}
@media(max-width:980px){body{overflow:auto}main{height:auto;grid-template-columns:1fr;grid-template-rows:auto}.timeline,.control-deck{grid-column:1;grid-row:auto}.video{height:55vh;min-height:260px}.metrics .metric{display:none}aside{grid-template-columns:1fr 1fr}}
</style></head><body>
<header><div class="brand"><div class="mark"><i></i><i></i><i></i></div><div><div class="eyebrow">EDGE AI · LIVE EVIDENCE</div><h1>EdgeFocus <span>视觉推理链</span></h1></div></div><div class="metrics"><div class="metric"><small>PREVIEW</small><b id="preview-fps">-- FPS</b></div><div class="metric"><small>NPU INFERENCE</small><b id="npu-metric">-- FPS · -- ms</b></div><div id="health" class="badge warn">IDLE</div></div></header>
<main><section class="panel stage"><div class="panel-head"><span><b class="index">01</b>视觉输入与证据叠加</span><span id="source" class="source">LIVE CAMERA</span></div><div id="video-stage" class="video"><div id="empty" class="empty">等待启动正式会话，或选择预录演示</div><img id="frame" alt="板端视觉输入"><video id="replay" muted playsinline preload="auto"></video><div class="scan"></div><svg id="overlay" class="overlay" preserveAspectRatio="none"><rect id="subject-box" fill="none" stroke="#ffca61" visibility="hidden"/><g id="skeleton"></g></svg><div class="hud"><span id="runtime-kind">FORMAL SESSION</span><br>RUN <b id="run-id">--</b><br>FRAME <b id="frame-id">--</b></div><div class="stamp"><span id="phase">IDLE</span><span id="frame-age">NO EVIDENCE</span></div></div><div class="toolbar"><button id="mode-live" class="mode-btn active">现场摄像头</button><button id="mode-recorded" class="mode-btn">预录 NPU</button><button id="mode-replay" class="mode-btn">纯前端演示</button><label class="switch"><input id="show-overlay" type="checkbox" checked>显示证据叠加</label><span id="mode-note" class="note">现场模式可由 App 或本页面启动正式会话</span></div></section>
<aside><section class="panel evidence"><span class="code">A</span><p>VISUAL · PRESENCE</p><h2 id="presence">等待证据</h2><small id="presence-detail">会话外不运行现场 NPU</small><b id="presence-score" class="score">--</b></section><section class="panel evidence amber"><span class="code">B</span><p>TEMPORAL MLP · PRONE</p><h2 id="prone">等待时序窗口</h2><small>时序模型持续确认，单帧不直接触发</small><b id="prone-score" class="score">--</b></section><section class="panel evidence violet"><span class="code">C</span><p>EXTERNAL FACT · PHONE</p><h2 id="phone">未使用</h2><small id="phone-source">现场模式由 App 蓝牙同步</small><div class="phone-buttons"><button data-phone="OFF">未使用</button><button data-phone="LEARNING">学习</button><button data-phone="ENTERTAINMENT">娱乐</button><button data-phone="GENERAL_OR_UNKNOWN">其他</button></div></section><section class="panel evidence red"><span class="code">D</span><p>FUSED DISTRACTION</p><div class="level"><div id="level" class="orb">—</div><div><h2 id="level-title">当前无分神等级</h2><div id="reasons" class="reason">UNKNOWN</div></div></div><div id="delivery" class="truth">正式会话：等待 App/手环投递状态</div></section></aside>
<section class="panel control-deck"><div class="session-control"><div class="session-head"><span><b class="index">02</b>现场会话控制</span><span id="session-clock" class="session-clock">00:00 / --:--</span></div><div class="session-body"><input id="session-duration" type="number" min="1" max="240" value="25" aria-label="专注时长"><span class="session-unit">分钟</span><div class="session-actions"><button class="start" data-session="start">启动</button><button class="confirm" data-session="confirm_precheck">确认预检</button><button data-session="pause">暂停</button><button data-session="resume">继续</button><button class="end" data-session="end">结束</button></div><span id="session-detail" class="session-detail">等待现场会话</span></div></div><div class="gimbal-control"><div class="gimbal-head"><span><b class="index">03</b><span id="gimbal-title">PRECHECK 云台微调</span></span><span id="gimbal-angle" class="gimbal-angle">X --° · Y --°</span></div><div class="gimbal-body"><div class="dpad"><button class="up" data-gimbal="up" aria-label="向上">↑</button><button class="left" data-gimbal="left" aria-label="向左">←</button><button class="center" data-gimbal="center" aria-label="回中">◎</button><button class="right" data-gimbal="right" aria-label="向右">→</button><button class="down" data-gimbal="down" aria-label="向下">↓</button></div><div class="gimbal-info"><b id="gimbal-state">等待预检</b><small id="gimbal-detail">正式会话进入 PRECHECK 后可用</small></div><div class="gimbal-actions"><button id="gimbal-takeover" class="takeover" data-gimbal="takeover">接管微调</button><button id="gimbal-release" class="release" data-gimbal="release">恢复跟踪</button><button id="gimbal-force-release" class="force" data-gimbal="force_release">强制释放</button><button id="gimbal-realign" class="realign" data-gimbal="recover">恢复云台</button></div></div></div></section>
<section class="panel timeline"><div class="panel-head"><span><b class="index">04</b>近 30 秒证据时间线</span><span class="legend"><span><i style="background:#5ee6a8"></i>人在</span><span><i style="background:#ffca61"></i>趴下</span><span><i style="background:#9f83ff"></i>手机上报</span><span><i style="background:#ff625f"></i>综合等级</span></span></div><canvas id="timeline"></canvas></section></main>
)HTML") + R"HTML(<script>
const $=id=>document.getElementById(id), frame=$('frame'), replay=$('replay'), overlay=$('overlay'), videoStage=$('video-stage');let previewEnabled=false,previewStreamKey='',previewReconnectTimer=0,previewRetry=0,previewTransportSuspended=document.hidden,lastPreviewSequence=0,lastPreviewAdvanceAt=performance.now(),mediaWidth=0,mediaHeight=0,heldVisual=null,heldVisualKey='',lastOverlayVisual=null,lastRun='',lastSampleAt=0,history=[],lastState=null,replayTrace=null,replayTraceClip='',replayTraceLoading='',replayVideoClip='',replayAdvanceBusy=false,gimbalBusy=false,sessionBusy=false,durationTouched=false,pollSerial=0,appliedPollSerial=0;
let gimbalOwner='',gimbalLeaseToken='';try{gimbalOwner=localStorage.getItem('edgefocus.gimbal.owner')||'';if(!gimbalOwner){gimbalOwner='web-'+(crypto.randomUUID?crypto.randomUUID():Date.now().toString(36)+'-'+Math.random().toString(36).slice(2));localStorage.setItem('edgefocus.gimbal.owner',gimbalOwner)}}catch(_){gimbalOwner='web-'+Date.now().toString(36)+'-'+Math.random().toString(36).slice(2)}
function nextGimbalSequence(){let n=0;try{n=Number(localStorage.getItem('edgefocus.gimbal.sequence')||0)+1;localStorage.setItem('edgefocus.gimbal.sequence',String(n))}catch(_){n=Date.now()}return n}
const fineBar=document.createElement('div');fineBar.className='phone-buttons';fineBar.innerHTML='<button data-gimbal="left" data-fine="true">← 微调</button><button data-gimbal="up" data-fine="true">↑ 微调</button><button data-gimbal="down" data-fine="true">↓ 微调</button><button data-gimbal="right" data-fine="true">→ 微调</button>';document.querySelector('.dpad').insertAdjacentElement('afterend',fineBar);
async function api(url,opt){const r=await fetch(url,opt),x=await r.json();if(!r.ok)throw Error(x.error||r.status);return x}const post=(url,data)=>api(url,{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(data)});const pct=v=>v==null?'--':Math.round(v*100)+'%';
function stopPreview(){previewEnabled=false;previewStreamKey='';previewRetry=0;clearTimeout(previewReconnectTimer);frame.style.display='none';if(frame.getAttribute('src'))frame.removeAttribute('src')}
function rebuildPreviewStream(){if(!previewEnabled||!previewStreamKey||previewTransportSuspended||document.hidden)return;frame.style.display='block';frame.src=`/api/demo/stream.mjpg?run=${encodeURIComponent(previewStreamKey)}&t=${Date.now()}`}
function schedulePreviewReconnect(){if(!previewEnabled||previewReconnectTimer||previewTransportSuspended||document.hidden)return;const delay=[500,1000,2000][Math.min(previewRetry,2)];previewRetry++;previewReconnectTimer=setTimeout(()=>{previewReconnectTimer=0;rebuildPreviewStream()},delay)}
function suspendPreviewTransport(){previewTransportSuspended=true;previewRetry=0;clearTimeout(previewReconnectTimer);previewReconnectTimer=0;frame.style.display='none';if(frame.getAttribute('src'))frame.removeAttribute('src')}
function resumePreviewTransport(){if(!previewTransportSuspended)return;previewTransportSuspended=false;if(!previewEnabled||!previewStreamKey)return;previewRetry=0;clearTimeout(previewReconnectTimer);previewReconnectTimer=0;rebuildPreviewStream()}
function startPreview(key){previewEnabled=true;if(previewStreamKey===key&&frame.getAttribute('src'))return;frame.style.display='none';if(frame.getAttribute('src'))frame.removeAttribute('src');previewStreamKey=key;previewRetry=0;clearTimeout(previewReconnectTimer);previewReconnectTimer=0;rebuildPreviewStream()}
)HTML" + R"HTML(
function unloadReplay(){replay.pause();replay.style.display='none';replayVideoClip='';replayAdvanceBusy=false;if(replay.getAttribute('src')){replay.removeAttribute('src');replay.load()}}
function requestReplayTrace(clip){if(!clip||replayTraceClip===clip||replayTraceLoading===clip)return;replayTraceLoading=clip;api('/api/demo/trace.json?clip='+encodeURIComponent(clip)).then(trace=>{if(replayTraceLoading!==clip)return;replayTrace=trace;replayTraceClip=clip;replayTraceLoading=''}).catch(()=>{if(replayTraceLoading===clip){replayTrace=null;replayTraceClip=clip;replayTraceLoading=''}})}
function scriptedState(s){if(s.source.mode!=='FRONTEND_REPLAY')return s;const clip=s.source.clip||'';requestReplayTrace(clip);if(!replay.duration||replayTraceClip!==clip||!replayTrace?.samples?.length)return s;const t=Math.round(replay.currentTime*1000),samples=replayTrace.samples;let sample=samples[0];for(const item of samples){if(item.media_timestamp_ms>t)break;sample=item}const x=structuredClone(s);x.source.media_timestamp_ms=t;x.visual=structuredClone(sample.visual);x.visual.media_timestamp_ms=t;x.phone=structuredClone(sample.phone);x.assessment=structuredClone(sample.assessment);return x}
function syncMediaRect(width=mediaWidth,height=mediaHeight){width=Number(width);height=Number(height);if(!(width>0&&height>0&&videoStage.clientWidth>0&&videoStage.clientHeight>0))return;mediaWidth=width;mediaHeight=height;const scale=Math.min(videoStage.clientWidth/width,videoStage.clientHeight/height),displayWidth=width*scale,displayHeight=height*scale,left=(videoStage.clientWidth-displayWidth)/2,top=(videoStage.clientHeight-displayHeight)/2;for(const el of [frame,replay,overlay]){el.style.left=left+'px';el.style.top=top+'px';el.style.width=displayWidth+'px';el.style.height=displayHeight+'px'}overlay.setAttribute('viewBox',`0 0 ${width} ${height}`)}
function visualForOverlay(s,replayMode){if(replayMode){heldVisual=null;heldVisualKey='';return s.visual}const key=`${s.source.mode}:${s.source.run_id}`;if(key!==heldVisualKey){heldVisualKey=key;heldVisual=null}const id=Number(s.visual?.frame_id||0);if(id>0&&(!heldVisual||id>Number(heldVisual.frame_id||0)))heldVisual=structuredClone(s.visual);return heldVisual||s.visual}
function drawOverlay(v){lastOverlayVisual=v;const box=$('subject-box'),g=$('skeleton');g.replaceChildren();if(!$('show-overlay').checked||!v?.subject_box||!(mediaWidth>0&&mediaHeight>0)){box.setAttribute('visibility','hidden');return}const b=v.subject_box,stroke=Math.max(2,Math.min(mediaWidth,mediaHeight)*.006),radius=Math.max(3,Math.min(mediaWidth,mediaHeight)*.009);box.setAttribute('x',b.x1*mediaWidth);box.setAttribute('y',b.y1*mediaHeight);box.setAttribute('width',(b.x2-b.x1)*mediaWidth);box.setAttribute('height',(b.y2-b.y1)*mediaHeight);box.setAttribute('stroke-width',stroke);box.setAttribute('vector-effect','non-scaling-stroke');box.setAttribute('visibility','visible');const pts=v.keypoints||[],links=[[5,6],[5,7],[7,9],[6,8],[8,10],[5,11],[6,12],[11,12],[11,13],[13,15],[12,14],[14,16],[0,5],[0,6]];for(const [a,bx] of links){if(pts[a]?.confidence>.25&&pts[bx]?.confidence>.25){const l=document.createElementNS('http://www.w3.org/2000/svg','line');for(const [k,val] of [['x1',pts[a].x*mediaWidth],['y1',pts[a].y*mediaHeight],['x2',pts[bx].x*mediaWidth],['y2',pts[bx].y*mediaHeight]])l.setAttribute(k,val);l.setAttribute('stroke','#49d6ff');l.setAttribute('stroke-width',stroke);l.setAttribute('vector-effect','non-scaling-stroke');g.append(l)}}pts.forEach(p=>{if(p.confidence<=.25)return;const c=document.createElementNS('http://www.w3.org/2000/svg','circle');c.setAttribute('cx',p.x*mediaWidth);c.setAttribute('cy',p.y*mediaHeight);c.setAttribute('r',radius);c.setAttribute('fill','#49d6ff');g.append(c)})}
function sample(s){const now=performance.now();if(now-lastSampleAt<240)return;lastSampleAt=now;const run=s.source.run_id;if(run!==lastRun){history=[];lastRun=run}history.push({t:Date.now(),p:s.visual.presence,q:s.visual.prone,phone:s.phone.active?1:0,l:s.assessment.level});const cutoff=Date.now()-30000;history=history.filter(x=>x.t>=cutoff);drawTimeline()}
function drawTimeline(){const c=$('timeline'),d=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;c.width=w*d;c.height=h*d;const x=c.getContext('2d');x.scale(d,d);x.clearRect(0,0,w,h);const rows=[['PRES','#5ee6a8','p'],['PRONE','#ffca61','q'],['PHONE','#9f83ff','phone'],['LEVEL','#ff625f','l']],left=44,rowH=h/4,now=Date.now();x.font='9px Consolas';rows.forEach((r,i)=>{const y=i*rowH+rowH/2;x.fillStyle='#607783';x.fillText(r[0],2,y+3);x.strokeStyle='#203343';x.beginPath();x.moveTo(left,y);x.lineTo(w,y);x.stroke();x.strokeStyle=r[1];x.lineWidth=2;x.beginPath();let started=false;history.forEach(v=>{const val=v[r[2]];if(val==null){started=false;return}const px=left+(w-left)*(1-(now-v.t)/30000),py=y-(r[2]==='l'?Math.min(3,val)/3:Math.max(0,Math.min(1,val)))*(rowH*.34);if(!started){x.moveTo(px,py);started=true}else x.lineTo(px,py)});x.stroke()})}
)HTML" + R"HTML(
const gimbalStateText={inactive:'未启用',searching:'自动搜索',centering:'自动居中',framing:'构图确认',manual:'手动微调',holding:'位置保持',degraded:'故障降级'};const gimbalReasonText={waiting_for_precheck:'等待预检',gimbal_unavailable:'云台能力不可用',formal_session_required:'等待启动正式会话',precheck_or_focusing_only:'仅预检或专注中可微调',gimbal_session_starting:'云台正在进入会话',gimbal_degraded:'云台已降级，等待恢复',manual_control_ready:'预检微调已就绪',session_manual_control_ready:'会话微调中，自动跟踪已暂停',automatic_adjustment_active:'自动控制中，可点击接管',realign_available:'云台可重新对齐',manual_lease_unavailable:'当前无手动控制租约'};
Object.assign(gimbalStateText,{maintenance_required:'需要维护',manual_available:'可人工接管',released:'已强制释放'});
const sessionReasonText={live_mode_required:'请先切换到现场摄像头',ready_to_start:'可从网页启动现场会话',session_active:'会话状态由主机统一管理'};const mmss=ms=>{if(ms==null||ms<0)return'--:--';const s=Math.floor(ms/1000);return String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0')};
function renderSession(s){const c=s.controls?.session||{},phase=s.session.active&&s.session.phase==='IDLE'?'PREPARING':s.session.phase;$('session-clock').textContent=`${mmss(s.session.elapsed_ms)} / ${mmss(s.session.target_duration_ms)}`;if(!durationTouched&&!s.session.active&&c.default_duration_minutes)$('session-duration').value=c.default_duration_minutes;$('session-duration').disabled=sessionBusy||s.session.active;document.querySelectorAll('[data-session]').forEach(b=>{const a=b.dataset.session,enabled=a==='start'?c.start_enabled:a==='confirm_precheck'?c.confirm_enabled:a==='pause'?c.pause_enabled:a==='resume'?c.resume_enabled:c.end_enabled;b.disabled=sessionBusy||!enabled});if(!sessionBusy)$('session-detail').textContent=`${phase} · ${sessionReasonText[c.reason]||c.reason||'等待状态'}`}
function renderGimbal(s){const g=s.gimbal||{},c=s.controls?.gimbal||{},phase=s.session.phase,isPrecheck=phase==='PRECHECK',ownsLease=!!g.manual_lease&&g.lease_owner_id===gimbalOwner&&!!gimbalLeaseToken,canAcquire=!!c.takeover_enabled&&!ownsLease,directPrecheck=!!c.manual_enabled&&isPrecheck,manualPossible=directPrecheck||ownsLease;$('gimbal-title').textContent=phase==='FOCUSING'?'SESSION 云台微调':'PRECHECK 云台微调';$('gimbal-takeover').textContent=isPrecheck?'跳过自动搜索':'接管微调';$('gimbal-takeover').dataset.runtimeAction=isPrecheck?'skip':'takeover';$('gimbal-angle').textContent=`X ${g.angle_x==null?'--':Number(g.angle_x).toFixed(1)}° · Y ${g.angle_y==null?'--':Number(g.angle_y).toFixed(1)}°`;$('gimbal-state').textContent=gimbalStateText[g.state]||'等待预检';if(!gimbalBusy)$('gimbal-detail').textContent=gimbalReasonText[c.reason]||g.detail||c.reason||'等待状态';document.querySelectorAll('[data-gimbal]').forEach(b=>{const a=b.dataset.gimbal,enabled=a==='takeover'?(isPrecheck?!!c.skip_search_enabled:canAcquire):a==='release'?c.release_enabled&&ownsLease:a==='recover'?c.recover_enabled:a==='force_release'?!!c.force_release_enabled:manualPossible;b.disabled=gimbalBusy||!enabled});$('gimbal-release').classList.toggle('show',phase==='FOCUSING'&&!!c.release_enabled);$('gimbal-realign').classList.toggle('show',!!c.recover_enabled);$('gimbal-force-release').classList.toggle('show',!!c.force_release_enabled);if(g.manual_lease&&g.lease_owner_id&&g.lease_owner_id!==gimbalOwner)gimbalLeaseToken=''}
async function runSession(action){if(sessionBusy)return;if(action==='end'&&!confirm('确认结束当前会话？'))return;sessionBusy=true;if(lastState)renderSession(lastState);$('session-detail').textContent='状态切换中，请稍候';const body={action};if(action==='start')body.duration_minutes=Number($('session-duration').value);else{body.session_id=lastState?.session?.id||'';if(action==='confirm_precheck'){body.owner_id=gimbalOwner;body.sequence=nextGimbalSequence();if(gimbalLeaseToken)body.lease_token=gimbalLeaseToken}}try{await post('/api/demo/session',body);await poll()}catch(e){$('session-detail').textContent=e.message||'会话命令失败'}finally{sessionBusy=false;if(lastState)renderSession(lastState)}}
async function runGimbal(action){if(gimbalBusy)return;gimbalBusy=true;if(lastState)renderGimbal(lastState);$('gimbal-detail').textContent='命令执行中，请稍候';try{const r=await post('/api/demo/gimbal',{action});$('gimbal-detail').textContent=r.accepted?'命令已执行':(r.error||'命令被拒绝');await poll()}catch(e){$('gimbal-detail').textContent=gimbalReasonText[e.message]||e.message||'云台命令失败'}finally{gimbalBusy=false;if(lastState)renderGimbal(lastState)}}
async function acquireGimbalLease(){const r=await post('/api/demo/gimbal',{action:'takeover',owner_id:gimbalOwner,sequence:nextGimbalSequence()});if(!r.accepted||!r.lease_token)throw Error(r.error||'manual_lease_unavailable');gimbalLeaseToken=r.lease_token;return r}
async function postGimbalV3(action,fineTune=false){const body={action,owner_id:gimbalOwner,sequence:nextGimbalSequence()};if(fineTune)body.fine_tune=true;if(gimbalLeaseToken)body.lease_token=gimbalLeaseToken;if(action==='force_release')body.confirmed=true;return post('/api/demo/gimbal',body)}
async function runGimbalV3(action,fineTune=false){if(gimbalBusy)return;if(action==='force_release'&&!confirm('强制释放会关闭云台保持，确认继续？'))return;gimbalBusy=true;if(lastState)renderGimbal(lastState);$('gimbal-detail').textContent='正在取得人工控制权';const needsLease=!['takeover','recover'].includes(action);try{let r;if(action==='takeover')r=await acquireGimbalLease();else{if(needsLease&&!gimbalLeaseToken)await acquireGimbalLease();$('gimbal-detail').textContent='命令执行中，请稍候';try{r=await postGimbalV3(action,fineTune)}catch(e){if(needsLease&&['manual_lease_unavailable','manual_lease_expired','manual_lease_mismatch'].includes(e.message)){gimbalLeaseToken='';await acquireGimbalLease();r=await postGimbalV3(action,fineTune)}else throw e}}if(action==='release'||action==='force_release')gimbalLeaseToken='';$('gimbal-detail').textContent=r.accepted?'命令已执行':(r.error||'命令被拒绝');await poll()}catch(e){if(['manual_lease_unavailable','manual_lease_expired','manual_lease_mismatch'].includes(e.message))gimbalLeaseToken='';$('gimbal-detail').textContent=gimbalReasonText[e.message]||e.message||'云台命令失败'}finally{gimbalBusy=false;if(lastState)renderGimbal(lastState)}}
async function keepGimbalLease(){const g=lastState?.gimbal;if(gimbalBusy||!gimbalLeaseToken||!g?.manual_lease||g.lease_owner_id!==gimbalOwner)return;try{const r=await post('/api/demo/gimbal',{action:'keepalive',owner_id:gimbalOwner,lease_token:gimbalLeaseToken,sequence:nextGimbalSequence()});if(!r.accepted)gimbalLeaseToken=''}catch(_){}}
function enforceExplicitManualLease(s){const g=s.gimbal||{},c=s.controls?.gimbal||{},isPrecheck=s.session?.phase==='PRECHECK',ownsLease=!!g.manual_lease&&g.lease_owner_id===gimbalOwner&&!!gimbalLeaseToken;if(g.manual_lease&&g.lease_owner_id&&g.lease_owner_id!==gimbalOwner)gimbalLeaseToken='';document.querySelectorAll('[data-gimbal]').forEach(b=>{const a=b.dataset.gimbal;if(['left','right','up','down','center'].includes(a))b.disabled=gimbalBusy||!(isPrecheck?!!c.manual_enabled:ownsLease);else if(a==='force_release')b.disabled=gimbalBusy||!c.force_release_enabled})}
runGimbalV3=async function(action,fineTune=false){if(gimbalBusy)return;const phase=lastState?.session?.phase||'',isPrecheck=phase==='PRECHECK',resolvedAction=action==='takeover'&&isPrecheck?'skip':action,needsLease=!isPrecheck&&!['takeover','recover','force_release'].includes(resolvedAction);if(needsLease&&!gimbalLeaseToken){$('gimbal-detail').textContent='请先点击接管微调';return}if(resolvedAction==='force_release'&&!confirm('强制释放会关闭云台保持，确认继续？'))return;gimbalBusy=true;if(lastState){renderGimbal(lastState);enforceExplicitManualLease(lastState)}$('gimbal-detail').textContent=resolvedAction==='takeover'?'正在取得人工控制权':resolvedAction==='skip'?'正在跳过自动搜索':'命令执行中，请稍候';try{const r=resolvedAction==='takeover'?await acquireGimbalLease():await postGimbalV3(resolvedAction,fineTune);if(resolvedAction==='release'||resolvedAction==='force_release')gimbalLeaseToken='';$('gimbal-detail').textContent=r.accepted?'命令已执行':(r.error||'命令被拒绝');await poll()}catch(e){if(['manual_lease_unavailable','manual_lease_expired','manual_lease_mismatch'].includes(e.message))gimbalLeaseToken='';$('gimbal-detail').textContent=gimbalReasonText[e.message]||e.message||'云台命令失败'}finally{gimbalBusy=false;if(lastState){renderGimbal(lastState);enforceExplicitManualLease(lastState)}}};
function render(raw){let s=scriptedState(raw);lastState=s;const replayMode=s.source.mode==='FRONTEND_REPLAY',liveMode=s.source.mode==='LIVE_CAMERA',recordedMode=s.source.mode==='RECORDED_NPU',overlayVisual=visualForOverlay(s,replayMode);$('run-id').textContent=String(s.source.run_id||'--').slice(-12);$('runtime-kind').textContent=s.source.formal?'FORMAL SESSION':liveMode?'FORMAL SESSION WINDOW':replayMode?'FRONTEND REPLAY':'DEMO RUNTIME';$('phase').textContent=s.session.phase;$('source').textContent=s.source.label;$('frame-id').textContent=overlayVisual?.frame_id||'--';$('preview-fps').textContent=replayMode?'VIDEO':`${Number(s.preview?.stream_fps||0).toFixed(1)} FPS`;$('npu-metric').textContent=replayMode?'NPU BYPASSED':`${Number(s.health.inference_fps||0).toFixed(1)} FPS · ${Number(s.health.inference_latency_ms||0).toFixed(0)} ms`;$('health').textContent=s.health.status;$('health').className='badge '+(s.health.status==='RUNNING'?'':replayMode?'replay':'warn');
const p=s.visual.presence,q=s.visual.prone,l=s.assessment.level;$('presence').textContent=p==null?'证据不足':p>=.5?'人在':'人不在';$('presence-score').textContent=pct(p);$('presence-detail').textContent=s.health.frame_age_ms<0&&!replayMode?'等待视觉证据':s.assessment.applicability==='NOT_PRESENT'?'等级不适用':replayMode?'同步展示轨迹':`证据年龄 ${s.health.frame_age_ms} ms`;$('prone').textContent=q==null?'等待时序窗口':q>=.5?'已确认趴下':'未趴下';$('prone-score').textContent=pct(q);$('phone').textContent=s.phone.active?({LEARNING:'学习类使用',ENTERTAINMENT:'娱乐类使用',GENERAL_OR_UNKNOWN:'其他使用'}[s.phone.category]||'使用中'):'未使用';$('phone-source').textContent=s.source.formal?'来自 App 蓝牙原始事实':liveMode?'现场模式只读，等待 App 会话':replayMode?'由同步回放轨迹驱动':'本地演示输入';$('level').textContent=l==null?'—':l;$('level-title').textContent=l==null?(s.assessment.applicability==='NOT_PRESENT'?'人不在 · 等级不适用':'当前无分神等级'):["未分神","轻度分神","持续分神","严重 / 长时分神"][l];$('reasons').textContent=(s.assessment.reasons||[]).join(' + ')||s.assessment.applicability;
document.querySelectorAll('[data-phone]').forEach(b=>{b.disabled=!s.controls.phone_controls_enabled||replayMode;b.classList.toggle('selected',(s.phone.active?b.dataset.phone===s.phone.category:b.dataset.phone==='OFF'))});$('mode-live').disabled=!s.controls.can_switch_mode;$('mode-recorded').disabled=!s.controls.can_switch_mode||!s.controls.video_ready;$('mode-replay').disabled=!s.controls.can_switch_mode||!s.controls.replay_ready;$('mode-live').classList.toggle('active',liveMode);$('mode-recorded').classList.toggle('active',recordedMode);$('mode-replay').classList.toggle('active',replayMode);$('mode-note').textContent=s.source.formal?'正式会话运行中，模式已锁定':liveMode?'现场模式可由 App 或本页面启动正式会话':replayMode?'纯前端 pre-log 回放 · 手机输入已锁定':'预录 NPU 模式不写正式会话或提醒';
renderSession(s);renderGimbal(s);enforceExplicitManualLease(s);
if(replayMode){stopPreview();const clip=s.source.clip||'';if(replayVideoClip!==clip||!replay.getAttribute('src')){replay.pause();if(replay.getAttribute('src')){replay.removeAttribute('src');replay.load()}replayVideoClip=clip;replay.src='/api/demo/recorded.mp4?clip='+encodeURIComponent(clip);replay.play().catch(()=>{})}replay.style.display='block';if(replay.videoWidth&&replay.videoHeight)syncMediaRect(replay.videoWidth,replay.videoHeight);$('empty').style.display='none'}else{unloadReplay();if(s.preview.available){const streamKey=`${s.source.mode}:${s.source.run_id}`;startPreview(streamKey);syncMediaRect(s.preview.width,s.preview.height);$('empty').style.display='none';const sequence=Number(s.preview.sequence||0);if(sequence!==lastPreviewSequence){lastPreviewSequence=sequence;lastPreviewAdvanceAt=performance.now()}else if(performance.now()-lastPreviewAdvanceAt>2000&&Number(s.preview.stream_fps||0)<=0){schedulePreviewReconnect()}}else{stopPreview();$('empty').style.display='block'}}$('frame-age').textContent=replayMode?`MEDIA ${Math.round(replay.currentTime||0)} s`:s.health.frame_age_ms>=0?`EVIDENCE ${s.health.frame_age_ms} ms OLD`:'NO EVIDENCE';drawOverlay(overlayVisual);sample(s)}
frame.addEventListener('load',function(){previewRetry=0;clearTimeout(previewReconnectTimer);previewReconnectTimer=0;this.style.display=previewEnabled&&!previewTransportSuspended?'block':'none'});frame.addEventListener('error',()=>{if(previewTransportSuspended||document.hidden)return;frame.style.display='none';schedulePreviewReconnect()});replay.addEventListener('loadedmetadata',()=>{syncMediaRect(replay.videoWidth,replay.videoHeight);if(lastOverlayVisual)drawOverlay(lastOverlayVisual)});replay.addEventListener('timeupdate',()=>{if(lastState?.source.mode==='FRONTEND_REPLAY')render(lastState)});replay.addEventListener('ended',()=>{if(lastState?.source.mode!=='FRONTEND_REPLAY'||replayAdvanceBusy)return;replayAdvanceBusy=true;history=[];post('/api/demo/mode',{mode:'replay'}).catch(()=>{}).finally(()=>{replayAdvanceBusy=false})});
$('mode-live').onclick=()=>post('/api/demo/mode',{mode:'live'});$('mode-recorded').onclick=()=>post('/api/demo/mode',{mode:'recorded'});$('mode-replay').onclick=()=>post('/api/demo/mode',{mode:'replay'});$('show-overlay').onchange=()=>lastOverlayVisual&&drawOverlay(lastOverlayVisual);$('session-duration').oninput=()=>durationTouched=true;document.querySelectorAll('[data-phone]').forEach(b=>b.onclick=()=>post('/api/demo/phone',{category:b.dataset.phone}));document.querySelectorAll('[data-session]').forEach(b=>b.onclick=()=>runSession(b.dataset.session));document.querySelectorAll('[data-gimbal]').forEach(b=>b.onclick=()=>runGimbalV3(b.dataset.gimbal,b.dataset.fine==='true'));
async function poll(){const serial=++pollSerial;try{const state=await api('/api/demo/state');if(serial<appliedPollSerial)return;appliedPollSerial=serial;render(state)}catch(e){$('health').textContent='OFFLINE';$('health').className='badge warn'}}poll();setInterval(poll,180);setInterval(keepGimbalLease,5000);new ResizeObserver(()=>{syncMediaRect();if(lastOverlayVisual)drawOverlay(lastOverlayVisual);drawTimeline()}).observe(videoStage);addEventListener('resize',()=>{syncMediaRect();if(lastOverlayVisual)drawOverlay(lastOverlayVisual);drawTimeline()});document.addEventListener('visibilitychange',()=>{document.hidden?suspendPreviewTransport():resumePreviewTransport()});addEventListener('blur',suspendPreviewTransport);addEventListener('focus',()=>{if(!document.hidden)resumePreviewTransport()});addEventListener('pagehide',suspendPreviewTransport);addEventListener('pageshow',()=>{if(!document.hidden)resumePreviewTransport()});
</script></body></html>)HTML";
}

std::string maintenancePageHtml() {
    return R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>EdgeFocus 云台维护</title>
<style>body{margin:0;background:#071018;color:#dcebed;font:14px system-ui}.wrap{max-width:900px;margin:32px auto;padding:24px}.card{border:1px solid #29404d;background:#0b1621;padding:18px;margin:12px 0}h1{font-size:25px}button,input{background:#0a131b;color:#dcebed;border:1px solid #41606f;padding:9px;margin:4px}button.danger{border-color:#a34b4b;color:#ff908d}pre{white-space:pre-wrap;color:#71e7ad}.muted{color:#78909d}</style></head><body><div class="wrap">
<h1>EdgeFocus · 云台维护向导</h1><p class="muted">隐藏维护入口。仅在无正式会话、无预录演示时开放；解锁 60 秒后自动失效。</p>
<div class="card"><b>一次性口令</b><p id="challenge">读取中</p><input id="phrase" placeholder="输入上方完整口令"><button onclick="unlock()">停止云台并解锁</button></div>
<div class="card"><button onclick="act('probe')">诊断</button><button onclick="act('arm_maintenance')">维护使能</button><button onclick="act('calibrate_electrical')">电气校准</button><button onclick="act('capture_origin')">保存机械正前方</button><button onclick="act('current_sample')">采集 1 秒电流</button><button onclick="act('stop')">STOP</button><br>
<input id="pp" placeholder="P_PEAK"><input id="pa" placeholder="P_AVG"><input id="yp" placeholder="Y_PEAK"><input id="ya" placeholder="Y_AVG"><input id="stall" placeholder="STALL_MS"><button onclick="protect()">写入电流保护</button><br>
<button class="danger" onclick="clearNvm('clear_origin')">清除原点</button><button class="danger" onclick="clearNvm('clear_all')">清除全部 NVM</button></div>
<pre id="out">等待状态</pre></div><script>
let token='';const out=document.getElementById('out');async function call(url,body){const r=await fetch(url,{method:body?'POST':'GET',headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});const j=await r.json();out.textContent=JSON.stringify(j,null,2);if(!r.ok)throw Error(j.error||'request failed');return j}
async function status(){try{const s=await call('/api/maintenance/status');challenge.textContent=s.challenge||s.reason||'不可用'}catch(e){challenge.textContent=e.message}}async function unlock(){const r=await call('/api/maintenance/unlock',{phrase:phrase.value});token=r.token||''}async function act(operation,extra={}){return call('/api/maintenance/action',{operation,token,...extra})}async function protect(){return act('protect_current_set',{pitch_peak:+pp.value,pitch_average:+pa.value,yaw_peak:+yp.value,yaw_average:+ya.value,stall_ms:+stall.value})}async function clearNvm(operation){if(!confirm('该操作会修改永久参数，确认继续？'))return;return act(operation,{confirmation:'CLEAR GIMBAL NVM'})}status();setInterval(status,2000)
</script></body></html>)HTML";
}
} // namespace

DemoHttpServer::DemoHttpServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                               DeviceManager& device_mgr, GimbalMonitor* gimbal_monitor,
                               gimbal::GimbalPrecheckController* gimbal_precheck,
                               session::SessionOrchestrator& sessions,
                               session::SessionSnapshotPublisher& snapshots,
                               PhoneFactBuffer& phone_facts,
                               PreviewFrameStore& preview_frames,
                               BoardDemoRuntime& demo_runtime,
                               int port)
    : Module("DemoHttpServer")
    , store_(store)
    , sqlite_store_(sqlite_store)
    , device_mgr_(device_mgr)
    , gimbal_monitor_(gimbal_monitor)
    , gimbal_precheck_(gimbal_precheck)
    , sessions_(sessions)
    , snapshots_(snapshots)
    , phone_facts_(phone_facts)
    , preview_frames_(preview_frames)
    , demo_runtime_(demo_runtime)
    , port_(port) {}

bool DemoHttpServer::init(AppContext& /*ctx*/) {
    if (port_ <= 0) return true;
#ifndef __linux__
    LOGW("DemoHttpServer", "Demo HTTP is only enabled on Linux builds");
    return true;
#else
    running_ = true;
    preview_encoder_thread_ = std::thread(&DemoHttpServer::previewEncoderLoop, this);
    server_thread_ = std::thread(&DemoHttpServer::serveLoop, this);
    return true;
#endif
}

void DemoHttpServer::runOnce(AppContext& /*ctx*/) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

void DemoHttpServer::shutdown(AppContext& /*ctx*/) {
    running_ = false;
    encoded_preview_changed_.notify_all();
    encoded_h264_changed_.notify_all();
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
    stopStreamClients();
    if (preview_encoder_thread_.joinable()) preview_encoder_thread_.join();
    if (h264_encoder_thread_.joinable()) h264_encoder_thread_.join();
    {
        std::lock_guard<std::mutex> lock(encoded_preview_mutex_);
        encoded_preview_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(encoded_h264_mutex_);
        encoded_h264_.reset();
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
        reapFinishedStreamClients();
        int client = ::accept(server_fd, nullptr, nullptr);
        if (client < 0) continue;
        timeval io_timeout{};
        // A 30-second demo clip is a few megabytes.  Two seconds was enough
        // for JSON/JPEG responses, but could expire mid-transfer on the board
        // Wi-Fi link and leave the browser with video audio/overlays only.
        // Keep the server bounded while allowing the existing send loop to
        // deliver the complete MP4 response.
        io_timeout.tv_sec = 15;
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

            if (request.method == "GET" &&
                request.path.rfind("/api/demo/stream.mp4", 0) == 0) {
                if (startH264StreamClient(client)) {
                    client = -1;
                } else {
                    response = httpResponse(
                        503, json{{"error", "H.264 preview unavailable"}}.dump());
                }
                have_response = true;
                break;
            }

            if (request.method == "GET" &&
                request.path.rfind("/api/demo/stream.mjpg", 0) == 0) {
                if (startPreviewStreamClient(client)) {
                    client = -1;
                } else {
                    response = httpResponse(
                        503, json{{"error", "preview stream client limit reached"}}.dump());
                }
                have_response = true;
                break;
            }

            response = handleRequest(request.method, request.path, request.body);
            have_response = true;
        }

        if (client < 0) continue;
        if (have_response) {
            (void)sendNoSignal(client, response.data(), response.size());
        }
        ::close(client);
    }

    ::close(server_fd);
#endif
}

DemoHttpServer::PreviewSourceSelection DemoHttpServer::previewSourceSelection() const {
    if (sessions_.active()) {
        const auto snapshot = snapshots_.read(nowMs());
        return {&preview_frames_, "formal:" + snapshot.context.session_id};
    }
    const auto status = demo_runtime_.status();
    if (status.mode == BoardDemoRuntime::Mode::RecordedNpu) {
        return {&demo_runtime_.previewFrames(), "recorded:" + std::to_string(status.run_id)};
    }
    return {};
}

void DemoHttpServer::previewEncoderLoop() {
#ifndef EDGEFOCUS_WITH_OPENCV
    while (running_) std::this_thread::sleep_for(std::chrono::milliseconds(100));
#else
    // Keep the browser preview below the board Wi-Fi's sustained throughput.
    // The inference/evidence path remains independent and keeps its own rate.
    constexpr std::int64_t kEncodePeriodMs = 100;
    constexpr int kJpegQuality = 65;
    std::string active_source;
    std::uint64_t last_sequence = 0;
    std::int64_t next_encode_ms = 0;
    std::int64_t last_encoded_ms = 0;
    double stream_fps = 0.0;

    while (running_) {
        const auto source = previewSourceSelection();
        if (!source.store || source.key.empty()) {
            if (!active_source.empty()) {
                active_source.clear();
                last_sequence = 0;
                next_encode_ms = 0;
                last_encoded_ms = 0;
                stream_fps = 0.0;
                {
                    std::lock_guard<std::mutex> lock(encoded_preview_mutex_);
                    encoded_preview_.reset();
                }
                encoded_preview_changed_.notify_all();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (source.key != active_source) {
            active_source = source.key;
            last_sequence = 0;
            next_encode_ms = 0;
            last_encoded_ms = 0;
            stream_fps = 0.0;
            {
                std::lock_guard<std::mutex> lock(encoded_preview_mutex_);
                encoded_preview_.reset();
            }
            encoded_preview_changed_.notify_all();
        }

        auto preview = source.store->waitForNewer(
            last_sequence, std::chrono::milliseconds(50));
        if (!running_) break;
        if (!preview || !preview->frame) {
            last_sequence = source.store->metadata().sequence;
            continue;
        }
        last_sequence = preview->sequence;

        auto encode_at = nowMs();
        if (next_encode_ms > encode_at) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(next_encode_ms - encode_at));
            if (!running_) break;
            if (auto newer = source.store->readAfter(last_sequence)) {
                preview = std::move(newer);
                last_sequence = preview->sequence;
            }
            encode_at = nowMs();
        }

        const auto current_source = previewSourceSelection();
        if (current_source.key != active_source || current_source.store != source.store) continue;

        auto encoded = std::make_shared<EncodedPreviewFrame>();
        encoded->source_key = active_source;
        encoded->sequence = preview->sequence;
        encoded->timestamp_ms = preview->frame->timestamp_ms;
        encoded->media_timestamp_ms = preview->frame->media_timestamp_ms;
        encoded->width = preview->frame->width;
        encoded->height = preview->frame->height;
        if (!encodePreviewJpeg(*preview->frame, kJpegQuality, encoded->jpeg)) continue;

        const auto finished_at = nowMs();
        if (last_encoded_ms > 0 && finished_at > last_encoded_ms) {
            const double instant_fps = 1000.0 /
                static_cast<double>(finished_at - last_encoded_ms);
            stream_fps = stream_fps <= 0.0
                ? instant_fps
                : stream_fps * 0.8 + instant_fps * 0.2;
        }
        last_encoded_ms = finished_at;
        encoded->stream_fps = stream_fps;
        next_encode_ms = encode_at + kEncodePeriodMs;

        if (previewSourceSelection().key != active_source) continue;
        {
            std::lock_guard<std::mutex> lock(encoded_preview_mutex_);
            encoded_preview_ = std::move(encoded);
        }
        encoded_preview_changed_.notify_all();
    }
#endif
}

void DemoHttpServer::h264EncoderLoop() {
#if !defined(EDGEFOCUS_WITH_OPENH264) || !defined(EDGEFOCUS_WITH_OPENCV)
    {
        std::lock_guard<std::mutex> lock(encoded_h264_mutex_);
        h264_runtime_.compiled = false;
        h264_runtime_.healthy = false;
        h264_runtime_.error = "OpenH264 preview not compiled";
    }
    while (running_) std::this_thread::sleep_for(std::chrono::milliseconds(100));
#else
    constexpr std::int64_t kEncodePeriodMs = 67;
    constexpr std::int64_t kCooldownMs = 5000;
    std::string active_source;
    std::uint64_t last_sequence = 0;
    std::int64_t next_encode_ms = 0;
    std::int64_t last_encoded_ms = 0;
    std::int64_t cooldown_until_ms = 0;
    int slow_encodes = 0;
    double stream_fps = 0.0;

    while (running_) {
        const auto source = previewSourceSelection();
        if (!source.store || source.key.empty()) {
            if (!active_source.empty()) {
                active_source.clear();
                last_sequence = 0;
                next_encode_ms = 0;
                last_encoded_ms = 0;
                stream_fps = 0.0;
                slow_encodes = 0;
                h264_encoder_.reset();
                std::lock_guard<std::mutex> lock(encoded_h264_mutex_);
                encoded_h264_.reset();
                h264_runtime_.active = false;
                h264_runtime_.stream_fps = 0.0;
            }
            encoded_h264_changed_.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (source.key != active_source) {
            active_source = source.key;
            last_sequence = 0;
            next_encode_ms = 0;
            last_encoded_ms = 0;
            stream_fps = 0.0;
            slow_encodes = 0;
            cooldown_until_ms = 0;
            h264_encoder_.reset();
            h264_force_keyframe_ = true;
            {
                std::lock_guard<std::mutex> lock(encoded_h264_mutex_);
                encoded_h264_.reset();
                h264_runtime_.active = true;
                h264_runtime_.stream_fps = 0.0;
            }
            encoded_h264_changed_.notify_all();
        }

        const auto now = nowMs();
        if (now < cooldown_until_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::min<std::int64_t>(100, cooldown_until_ms - now)));
            continue;
        }
        auto preview = source.store->waitForNewer(
            last_sequence, std::chrono::milliseconds(50));
        if (!running_) break;
        if (!preview || !preview->frame) continue;
        last_sequence = preview->sequence;
        auto encode_at = nowMs();
        if (next_encode_ms > encode_at) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(next_encode_ms - encode_at));
            if (!running_) break;
            if (auto newer = source.store->readAfter(last_sequence)) {
                preview = std::move(newer);
                last_sequence = preview->sequence;
            }
            encode_at = nowMs();
        }
        if (previewSourceSelection().key != active_source) continue;
        if (h264_force_keyframe_.exchange(false)) h264_encoder_.forceKeyframe();

        auto packet = std::make_shared<H264PreviewPacket>();
        const bool encoded = h264_encoder_.encode(
            *preview->frame, active_source, preview->sequence, *packet);
        const auto health = h264_encoder_.health();
        next_encode_ms = encode_at + kEncodePeriodMs;
        // Normal A35 jitter can reach 65-80 ms while NPU work is active. Only
        // shed AVC on a sustained real stall, not on harmless cadence jitter.
        if (health.encode_ms > 110.0) ++slow_encodes;
        else slow_encodes = std::max(0, slow_encodes - 1);

        const bool encoder_overloaded = slow_encodes >= 8;
        if (!encoded || health.consecutive_failures >= 3 || encoder_overloaded) {
            if (health.consecutive_failures >= 3 || encoder_overloaded) {
                cooldown_until_ms = nowMs() + kCooldownMs;
                h264_encoder_.reset();
                slow_encodes = 0;
            }
            std::lock_guard<std::mutex> lock(encoded_h264_mutex_);
            h264_runtime_.compiled = health.compiled;
            h264_runtime_.healthy = false;
            h264_runtime_.active = true;
            h264_runtime_.consecutive_failures = health.consecutive_failures;
            h264_runtime_.encode_ms = health.encode_ms;
            h264_runtime_.error = encoder_overloaded
                ? "software encoder overloaded" : health.error;
            continue;
        }
        if (packet->media_fragment.empty()) continue;

        const auto finished_at = nowMs();
        if (last_encoded_ms > 0 && finished_at > last_encoded_ms) {
            const double instant_fps = 1000.0 /
                static_cast<double>(finished_at - last_encoded_ms);
            stream_fps = stream_fps <= 0.0
                ? instant_fps : stream_fps * 0.8 + instant_fps * 0.2;
        }
        last_encoded_ms = finished_at;
        packet->encoded_at_ms = finished_at;
        if (previewSourceSelection().key != active_source) continue;
        {
            std::lock_guard<std::mutex> lock(encoded_h264_mutex_);
            encoded_h264_ = std::move(packet);
            h264_runtime_.compiled = health.compiled;
            h264_runtime_.healthy = true;
            h264_runtime_.active = true;
            h264_runtime_.consecutive_failures = 0;
            h264_runtime_.encode_ms = health.encode_ms;
            h264_runtime_.stream_fps = stream_fps;
            h264_runtime_.error.clear();
        }
        encoded_h264_changed_.notify_all();
    }
#endif
}

bool DemoHttpServer::startPreviewStreamClient(int client_fd) {
#ifndef __linux__
    (void)client_fd;
    return false;
#else
    reapFinishedStreamClients();
    std::lock_guard<std::mutex> lock(stream_clients_mutex_);
    if (stream_clients_.size() >= 2) return false;

    timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    auto state = std::make_shared<StreamClientState>();
    state->fd = client_fd;
    StreamClient client;
    client.state = state;
    client.worker = std::thread(&DemoHttpServer::previewStreamClientLoop, this, state);
    stream_clients_.push_back(std::move(client));
    return true;
#endif
}

bool DemoHttpServer::startH264StreamClient(int client_fd) {
#if !defined(__linux__) || !defined(EDGEFOCUS_WITH_OPENH264)
    (void)client_fd;
    return false;
#else
    {
        std::lock_guard<std::mutex> status_lock(encoded_h264_mutex_);
        if (!h264_runtime_.compiled || !h264_runtime_.active) return false;
    }
    reapFinishedStreamClients();
    std::lock_guard<std::mutex> lock(stream_clients_mutex_);
    if (stream_clients_.size() >= 6) return false;
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 600000;
    ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    int send_buffer = 64 * 1024;
    ::setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
    auto state = std::make_shared<StreamClientState>();
    state->fd = client_fd;
    StreamClient client;
    client.state = state;
    client.worker = std::thread(&DemoHttpServer::h264StreamClientLoop, this, state);
    stream_clients_.push_back(std::move(client));
    h264_force_keyframe_ = true;
    return true;
#endif
}

void DemoHttpServer::previewStreamClientLoop(
    const std::shared_ptr<StreamClientState>& state) {
#ifdef __linux__
    const int fd = state->fd.load();
    const std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=edgefocusframe\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n";
    bool connected = sendNoSignal(fd, header.data(), header.size());
    std::string last_source;
    std::uint64_t last_sequence = 0;
    while (connected && running_) {
        std::shared_ptr<const EncodedPreviewFrame> frame;
        {
            std::unique_lock<std::mutex> lock(encoded_preview_mutex_);
            encoded_preview_changed_.wait_for(lock, std::chrono::milliseconds(500), [&] {
                return !running_ || (encoded_preview_ &&
                    (encoded_preview_->source_key != last_source ||
                     encoded_preview_->sequence != last_sequence));
            });
            if (!running_) break;
            frame = encoded_preview_;
        }
        if (!frame || frame->jpeg.empty()) continue;
        last_source = frame->source_key;
        last_sequence = frame->sequence;
        std::ostringstream part;
        part << "--edgefocusframe\r\n"
             << "Content-Type: image/jpeg\r\n"
             << "Content-Length: " << frame->jpeg.size() << "\r\n"
             << "X-EdgeFocus-Source: " << frame->source_key << "\r\n"
             << "X-EdgeFocus-Sequence: " << frame->sequence << "\r\n"
             << "X-EdgeFocus-Timestamp: " << frame->timestamp_ms << "\r\n\r\n";
        const auto part_header = part.str();
        connected = sendNoSignal(fd, part_header.data(), part_header.size()) &&
                    sendNoSignal(fd, frame->jpeg.data(), frame->jpeg.size()) &&
                    sendNoSignal(fd, "\r\n", 2);
    }
    const int owned_fd = state->fd.exchange(-1);
    if (owned_fd >= 0) {
        ::shutdown(owned_fd, SHUT_RDWR);
        ::close(owned_fd);
    }
#endif
    state->done = true;
}

void DemoHttpServer::h264StreamClientLoop(
    const std::shared_ptr<StreamClientState>& state) {
#ifdef __linux__
    const int fd = state->fd.load();
    const std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: video/mp4\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n";
    bool connected = sendNoSignal(fd, header.data(), header.size());
    std::string last_source;
    std::uint64_t last_sequence = 0;
    std::int64_t last_timestamp_ms = 0;
    bool initialized = false;
    while (connected && running_) {
        std::shared_ptr<const H264PreviewPacket> packet;
        {
            std::unique_lock<std::mutex> lock(encoded_h264_mutex_);
            encoded_h264_changed_.wait_for(lock, std::chrono::milliseconds(500), [&] {
                return !running_ || (encoded_h264_ &&
                    (encoded_h264_->source_key != last_source ||
                     encoded_h264_->sequence != last_sequence));
            });
            if (!running_) break;
            packet = encoded_h264_;
        }
        if (!packet || packet->media_fragment.empty()) continue;
        if (packet->source_key != last_source) {
            last_source = packet->source_key;
            initialized = false;
            last_timestamp_ms = 0;
        }
        last_sequence = packet->sequence;
        if (last_timestamp_ms > 0 && packet->timestamp_ms - last_timestamp_ms > 250) {
            initialized = false;
        }
        last_timestamp_ms = packet->timestamp_ms;
        if (!initialized) {
            if (!packet->keyframe || packet->initialization_segment.empty()) {
                h264_force_keyframe_ = true;
                continue;
            }
            connected = sendNoSignal(fd, packet->initialization_segment.data(),
                                     packet->initialization_segment.size());
            initialized = connected;
        }
        if (initialized && connected) {
            connected = sendNoSignal(fd, packet->media_fragment.data(),
                                     packet->media_fragment.size());
        }
    }
    const int owned_fd = state->fd.exchange(-1);
    if (owned_fd >= 0) {
        ::shutdown(owned_fd, SHUT_RDWR);
        ::close(owned_fd);
    }
#endif
    state->done = true;
}

void DemoHttpServer::reapFinishedStreamClients() {
    std::lock_guard<std::mutex> lock(stream_clients_mutex_);
    for (auto it = stream_clients_.begin(); it != stream_clients_.end();) {
        if (!it->state->done.load()) {
            ++it;
            continue;
        }
        if (it->worker.joinable()) it->worker.join();
        it = stream_clients_.erase(it);
    }
}

void DemoHttpServer::stopStreamClients() {
    std::vector<StreamClient> clients;
    {
        std::lock_guard<std::mutex> lock(stream_clients_mutex_);
        clients.swap(stream_clients_);
    }
#ifdef __linux__
    for (auto& client : clients) {
        const int fd = client.state->fd.load();
        if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
    }
#endif
    encoded_preview_changed_.notify_all();
    encoded_h264_changed_.notify_all();
    for (auto& client : clients) {
        if (client.worker.joinable()) client.worker.join();
    }
}

std::string DemoHttpServer::handleRequest(const std::string& method, const std::string& path,
                                          const std::string& body) {
    try {
        if (method == "OPTIONS") return httpResponse(200, "{}");
        if (method == "GET" && (path == "/" || path == "/demo")) {
            return httpResponse(200, runtimeDemoPageHtml(), "text/html; charset=utf-8");
        }
        if (method == "GET" && path == "/maintenance") {
            return httpResponse(200, maintenancePageHtml(), "text/html; charset=utf-8");
        }
        if (method == "GET" && path == "/api/maintenance/status") {
            const auto response = maintenanceStatus();
            return httpResponse(response.value("eligible", false) ? 200 : 409,
                                response.dump());
        }
        if (method == "POST" && path == "/api/maintenance/unlock") {
            const json data = body.empty() ? json::object() : json::parse(body);
            const auto response = handleMaintenanceUnlock(data);
            return httpResponse(response.value("success", false) ? 200 : 409,
                                response.dump());
        }
        if (method == "POST" && path == "/api/maintenance/action") {
            const json data = body.empty() ? json::object() : json::parse(body);
            const auto response = handleMaintenanceAction(data);
            return httpResponse(response.value("success", false) ? 200 : 409,
                                response.dump());
        }
        if (method == "GET" && path == "/api/demo/state") {
            return httpResponse(200, runtimeSnapshotJson().dump());
        }
        if (method == "GET" && path.rfind("/api/demo/frame.jpg", 0) == 0) {
            return previewFrameResponse();
        }
        if (method == "GET" && path.rfind("/api/demo/recorded.mp4", 0) == 0) {
            return recordedVideoResponse();
        }
        if (method == "GET" && path.rfind("/api/demo/trace.json", 0) == 0) {
            return replayTraceResponse();
        }
        if (method == "POST" && path == "/api/demo/mode") {
            const json data = body.empty() ? json::object() : json::parse(body);
            const auto response = handleDemoMode(data);
            return httpResponse(response.value("success", false) ? 200 : 409, response.dump());
        }
        if (method == "POST" && path == "/api/demo/session") {
            const json data = body.empty() ? json::object() : json::parse(body);
            const auto response = handleRuntimeSession(data);
            return httpResponse(response.value("success", false) ? 200 : 409,
                                response.dump());
        }
        if (method == "POST" && path == "/api/demo/phone") {
            const json data = body.empty() ? json::object() : json::parse(body);
            const auto response = handleRuntimePhone(data);
            return httpResponse(response.value("accepted", false) ? 200 : 400, response.dump());
        }
        if (method == "POST" && path == "/api/demo/gimbal") {
            const json data = body.empty() ? json::object() : json::parse(body);
            const auto response = handleRuntimeGimbal(data);
            return httpResponse(response.value("accepted", false) ? 200 : 409, response.dump());
        }
        // Legacy mutation endpoints are intentionally no longer routed. Demo
        // state can only come from SessionSnapshotPublisher.
        return httpResponse(404, json{{"status", "error"}, {"error", "unknown endpoint"}}.dump());
    } catch (const std::exception& e) {
        return httpResponse(400, json{{"status", "error"}, {"error", e.what()}}.dump());
    }
}

bool DemoHttpServer::maintenanceEligible(std::string& reason) const {
    if (!gimbal_precheck_) {
        reason = "gimbal_unavailable";
        return false;
    }
    if (sessions_.active()) {
        reason = "formal_session_active";
        return false;
    }
    if (demo_runtime_.status().mode != BoardDemoRuntime::Mode::LiveCamera) {
        reason = "demo_runtime_active";
        return false;
    }
    const auto state = gimbal_precheck_->snapshot().state;
    if (state != gimbal::PrecheckState::Inactive &&
        state != gimbal::PrecheckState::MaintenanceRequired &&
        state != gimbal::PrecheckState::Degraded) {
        reason = "gimbal_controller_busy";
        return false;
    }
    reason = "ready";
    return true;
}

std::string DemoHttpServer::nextMaintenanceNonce() {
    std::ostringstream value;
    value << std::hex << static_cast<std::uint64_t>(nowMs()) << '-'
          << maintenance_nonce_.fetch_add(1);
    return value.str();
}

DemoHttpServer::json DemoHttpServer::maintenanceStatus() {
    std::string reason;
    const bool eligible = maintenanceEligible(reason);
    std::lock_guard<std::mutex> lock(maintenance_mutex_);
    const auto now = nowMs();
    if (maintenance_token_expires_ms_ <= now) {
        maintenance_token_.clear();
        maintenance_token_expires_ms_ = 0;
    }
    if (maintenance_challenge_.empty()) {
        maintenance_challenge_ = "EDGEFOCUS-MAINT-" + nextMaintenanceNonce();
    }
    json response{{"eligible", eligible}, {"reason", reason},
                  {"challenge", eligible ? maintenance_challenge_ : ""},
                  {"unlocked", !maintenance_token_.empty()},
                  {"expires_at_ms", maintenance_token_expires_ms_}};
    if (gimbal_precheck_) {
        const auto hardware = gimbal_precheck_->snapshotHardware();
        response["gimbal"] = {
            {"state", gimbal::stateName(hardware.state)},
            {"protocol", hardware.snapshot.protocol_version},
            {"calibration_valid", hardware.snapshot.calibration_valid},
            {"origin_valid", hardware.snapshot.origin_valid},
            {"calibration_epoch", hardware.snapshot.calibration_epoch},
            {"origin_epoch", hardware.snapshot.origin_epoch},
            {"current_protection", hardware.snapshot.current_protection_state},
            {"operation", hardware.snapshot.operation},
            {"operation_generation", hardware.snapshot.operation_generation
                ? json(*hardware.snapshot.operation_generation) : json(nullptr)},
            {"operation_progress", hardware.snapshot.operation_progress
                ? json(*hardware.snapshot.operation_progress) : json(nullptr)},
            {"last_operation_status", hardware.snapshot.last_operation_status},
        };
    }
    return response;
}

DemoHttpServer::json DemoHttpServer::handleMaintenanceUnlock(const json& data) {
    std::string reason;
    if (!maintenanceEligible(reason)) {
        return {{"success", false}, {"error", reason}};
    }
    if (!data.contains("phrase") || !data["phrase"].is_string()) {
        return {{"success", false}, {"error", "maintenance_phrase_required"}};
    }
    {
        std::lock_guard<std::mutex> lock(maintenance_mutex_);
        if (maintenance_challenge_.empty() ||
            data["phrase"].get<std::string>() != maintenance_challenge_) {
            return {{"success", false}, {"error", "maintenance_phrase_mismatch"}};
        }
    }
    const auto stopped = gimbal_precheck_->executeMaintenance(
        {gimbal::MaintenanceOperation::Stop}, 2000);
    const auto hardware = gimbal_precheck_->snapshotHardware();
    if (!stopped.success || hardware.state != gimbal::State::Disabled) {
        return {{"success", false}, {"error", "gimbal_stop_before_unlock_failed"}};
    }
    std::lock_guard<std::mutex> lock(maintenance_mutex_);
    maintenance_token_ = "maint-token-" + nextMaintenanceNonce();
    maintenance_token_expires_ms_ = nowMs() + 60000;
    maintenance_challenge_.clear();
    return {{"success", true}, {"token", maintenance_token_},
            {"expires_at_ms", maintenance_token_expires_ms_}};
}

DemoHttpServer::json DemoHttpServer::handleMaintenanceAction(const json& data) {
    std::string reason;
    if (!maintenanceEligible(reason)) {
        return {{"success", false}, {"error", reason}};
    }
    if (!data.contains("token") || !data["token"].is_string()) {
        return {{"success", false}, {"error", "maintenance_token_required"}};
    }
    {
        std::lock_guard<std::mutex> lock(maintenance_mutex_);
        if (maintenance_token_.empty() || nowMs() >= maintenance_token_expires_ms_ ||
            data["token"].get<std::string>() != maintenance_token_) {
            return {{"success", false}, {"error", "maintenance_token_invalid_or_expired"}};
        }
    }
    const auto operation = lowerCopy(data.value("operation", ""));
    gimbal::CommandResult result;
    if (operation == "probe") {
        result = gimbal_precheck_->executeMaintenance({gimbal::MaintenanceOperation::Probe});
    } else if (operation == "stop") {
        result = gimbal_precheck_->executeMaintenance({gimbal::MaintenanceOperation::Stop});
    } else if (operation == "arm_maintenance") {
        result = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::Probe});
    } else if (operation == "calibrate_electrical") {
        result = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::ArmMaintenance});
        if (result.success) {
            result = gimbal_precheck_->executeMaintenance(
                {gimbal::MaintenanceOperation::CalibrateElectricalAll}, 5000);
        }
    } else if (operation == "capture_origin") {
        result = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::Probe});
        if (result.success) result = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::CaptureOrigin});
        const auto stopped = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::Stop});
        if (result.success && !stopped.success) result = stopped;
    } else if (operation == "current_sample") {
        result = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::ArmMaintenance});
        if (result.success) result = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::Hold});
        auto sample_started = std::chrono::steady_clock::now();
        if (result.success) {
            sample_started = std::chrono::steady_clock::now();
            result = gimbal_precheck_->executeMaintenance(
                {gimbal::MaintenanceOperation::CurrentReset});
        }
        if (result.success) result = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::VerifyMotionPattern}, 30000);
        if (result.success) {
            const auto elapsed = std::chrono::steady_clock::now() - sample_started;
            if (elapsed < std::chrono::milliseconds(1100)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1100) - elapsed);
            }
            result = gimbal_precheck_->executeMaintenance(
                {gimbal::MaintenanceOperation::CurrentRead});
        }
        if (result.success && (!result.current || !result.current->valid ||
                               result.current->saturated ||
                               result.current->window_ms < 1000)) {
            result.success = false;
            result.error = "current_sample_not_usable";
        }
        const auto stopped = gimbal_precheck_->executeMaintenance(
            {gimbal::MaintenanceOperation::Stop});
        if (result.success && !stopped.success) result = stopped;
    } else if (operation == "protect_current_set") {
        const auto p_peak = data.value("pitch_peak", 0u);
        const auto p_average = data.value("pitch_average", 0u);
        const auto y_peak = data.value("yaw_peak", 0u);
        const auto y_average = data.value("yaw_average", 0u);
        const auto stall_ms = data.value("stall_ms", 0u);
        if (p_peak > 65535u || p_average > 65535u || y_peak > 65535u ||
            y_average > 65535u || stall_ms > 65535u) {
            return {{"success", false}, {"error", "current_protection_value_out_of_range"}};
        }
        gimbal::MaintenanceRequest request{gimbal::MaintenanceOperation::ProtectCurrentSet};
        request.protection = gimbal::CurrentProtectionConfig{
            static_cast<std::uint16_t>(p_peak), static_cast<std::uint16_t>(p_average),
            static_cast<std::uint16_t>(y_peak), static_cast<std::uint16_t>(y_average),
            static_cast<std::uint16_t>(stall_ms)};
        result = gimbal_precheck_->executeMaintenance({gimbal::MaintenanceOperation::Probe});
        if (result.success) result = gimbal_precheck_->executeMaintenance(request);
    } else if (operation == "clear_origin" || operation == "clear_all") {
        if (data.value("confirmation", "") != "CLEAR GIMBAL NVM") {
            return {{"success", false}, {"error", "nvm_second_confirmation_required"}};
        }
        result = gimbal_precheck_->executeMaintenance({gimbal::MaintenanceOperation::Probe});
        if (result.success) {
            result = gimbal_precheck_->executeMaintenance({
                operation == "clear_all" ? gimbal::MaintenanceOperation::ClearAll
                                         : gimbal::MaintenanceOperation::ClearOrigin});
        }
    } else {
        return {{"success", false}, {"error", "unknown_maintenance_operation"}};
    }

    json response{{"success", result.success}, {"error", result.error},
                  {"firmware_accepted", result.firmware_accepted},
                  {"settled", result.settled}, {"transcript", result.transcript}};
    if (result.current) {
        response["current"] = {{"pitch_raw", result.current->pitch_raw},
                               {"pitch_average", result.current->pitch_average},
                               {"pitch_peak", result.current->pitch_peak},
                               {"yaw_raw", result.current->yaw_raw},
                               {"yaw_average", result.current->yaw_average},
                               {"yaw_peak", result.current->yaw_peak},
                               {"window_ms", result.current->window_ms},
                               {"samples", result.current->samples},
                               {"valid", result.current->valid},
                               {"saturated", result.current->saturated}};
    }
    return response;
}

DemoHttpServer::json DemoHttpServer::runtimeSnapshotJson() const {
    const bool formal_session = sessions_.active();
    const auto demo_status = demo_runtime_.status();
    const auto runtime = formal_session ? snapshots_.read(nowMs()) : demo_runtime_.snapshot();
    const auto legacy = store_.snapshot();
    const auto preview = formal_session ? preview_frames_.metadata()
                                        : demo_runtime_.previewFrames().metadata();
    double preview_stream_fps = 0.0;
    std::uint64_t encoded_preview_sequence = 0;
    std::int64_t encoded_preview_timestamp_ms = 0;
    std::int64_t encoded_preview_age_ms = -1;
    H264PreviewRuntime h264_preview;
    std::uint64_t h264_sequence = 0;
    std::int64_t h264_age_ms = -1;
    const auto selected_preview_source = previewSourceSelection();
    {
        std::lock_guard<std::mutex> lock(encoded_preview_mutex_);
        if (encoded_preview_ &&
            encoded_preview_->source_key == selected_preview_source.key) {
            preview_stream_fps = encoded_preview_->stream_fps;
            encoded_preview_sequence = encoded_preview_->sequence;
            encoded_preview_timestamp_ms = encoded_preview_->timestamp_ms;
            encoded_preview_age_ms = std::max<std::int64_t>(
                0, nowMs() - encoded_preview_->timestamp_ms);
        }
    }
    {
        std::lock_guard<std::mutex> lock(encoded_h264_mutex_);
        h264_preview = h264_runtime_;
        if (encoded_h264_ &&
            encoded_h264_->source_key == selected_preview_source.key) {
            h264_sequence = encoded_h264_->sequence;
            h264_age_ms = std::max<std::int64_t>(
                0, nowMs() - encoded_h264_->timestamp_ms);
        }
    }
    const auto now_ms = nowMs();
    const bool gimbal_precheck_phase = formal_session && runtime.phase == session::Phase::Precheck;
    const bool gimbal_focusing_phase = formal_session && runtime.phase == session::Phase::Focusing;
    const bool gimbal_control_phase = gimbal_precheck_phase || gimbal_focusing_phase;
    const auto gimbal_control = gimbal_precheck_
        ? std::optional<gimbal::PrecheckStatus>(gimbal_precheck_->snapshot())
        : std::nullopt;
    const bool gimbal_session_matches = gimbal_control &&
        gimbal_control->session_id == runtime.context.session_id;
    const bool gimbal_precheck_manual_enabled = gimbal_precheck_phase &&
        gimbal_session_matches && !runtime.gimbal.health.degraded &&
        gimbal_control->state == gimbal::PrecheckState::ManualAvailable;
    const bool gimbal_session_manual_enabled = gimbal_focusing_phase &&
        gimbal_session_matches && !runtime.gimbal.health.degraded &&
        gimbal_control->state == gimbal::PrecheckState::Manual &&
        gimbal_control->manual_lease;
    const bool gimbal_manual_enabled = gimbal_precheck_manual_enabled ||
                                       gimbal_session_manual_enabled;
    const bool gimbal_skip_search_enabled = gimbal_precheck_phase &&
        gimbal_session_matches && !runtime.gimbal.health.degraded &&
        !gimbal_control->search_completed &&
        (gimbal_control->state == gimbal::PrecheckState::Searching ||
         gimbal_control->state == gimbal::PrecheckState::Centering ||
         gimbal_control->state == gimbal::PrecheckState::Framing ||
         gimbal_control->state == gimbal::PrecheckState::Holding);
    const bool gimbal_takeover_enabled = gimbal_focusing_phase &&
        gimbal_session_matches && !runtime.gimbal.health.degraded &&
        !gimbal_session_manual_enabled &&
        gimbal_control->state == gimbal::PrecheckState::Holding;
    const bool gimbal_release_enabled = gimbal_focusing_phase &&
                                        gimbal_session_manual_enabled;
    const bool gimbal_force_release_enabled =
        gimbal_control_phase && gimbal_session_matches &&
        gimbal_control->state != gimbal::PrecheckState::Inactive &&
        gimbal_control->state != gimbal::PrecheckState::Released;
    const bool gimbal_recover_enabled = gimbal_control_phase && gimbal_session_matches &&
        runtime.gimbal.recoverable;
    std::string gimbal_control_reason = "waiting_for_precheck";
    if (!gimbal_precheck_) gimbal_control_reason = "gimbal_unavailable";
    else if (!formal_session) gimbal_control_reason = "formal_session_required";
    else if (!gimbal_control_phase) gimbal_control_reason = "precheck_or_focusing_only";
    else if (!gimbal_session_matches) gimbal_control_reason = "gimbal_session_starting";
    else if (runtime.gimbal.health.degraded) gimbal_control_reason = "gimbal_degraded";
    else if (gimbal_session_manual_enabled) gimbal_control_reason = "session_manual_control_ready";
    else if (gimbal_precheck_manual_enabled) gimbal_control_reason = "manual_control_ready";
    else if (gimbal_skip_search_enabled) gimbal_control_reason = "automatic_adjustment_active";
    else if (gimbal_takeover_enabled) gimbal_control_reason = "automatic_adjustment_active";
    else if (gimbal_control &&
             gimbal_control->state == gimbal::PrecheckState::MaintenanceRequired) {
        gimbal_control_reason = "maintenance_required";
    } else if (gimbal_control &&
               gimbal_control->state == gimbal::PrecheckState::Released) {
        gimbal_control_reason = "gimbal_released";
    } else if (gimbal_recover_enabled) gimbal_control_reason = "realign_available";
    else gimbal_control_reason = "manual_lease_unavailable";

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
    const bool evidence_available = runtime.visual.evidence_timestamp_ms > 0;
    const auto frame_age_ms = evidence_available
                                  ? std::max<std::int64_t>(0, now_ms - runtime.visual.evidence_timestamp_ms)
                                  : -1;
    std::string source_label = "LIVE CAMERA · WAITING FOR APP SESSION";
    std::string health_status = "IDLE";
    if (formal_session) {
        source_label = "LIVE CAMERA · FORMAL SESSION";
        health_status = legacy.device.camera_ok ? "RUNNING" : "STARTING";
    } else if (demo_status.mode == BoardDemoRuntime::Mode::RecordedNpu) {
        source_label = "RECORDED INPUT · REAL NPU";
        health_status = frame_age_ms >= 0 && frame_age_ms <= 2000 ? "RUNNING" : "STARTING";
    } else if (demo_status.mode == BoardDemoRuntime::Mode::FrontendReplay) {
        source_label = "FRONTEND REPLAY · PRE-LOG · " +
            std::filesystem::path(demo_status.video_path).filename().string();
        health_status = "REPLAY";
    }
    json box = nullptr;
    if (runtime.visual.subject_box) {
        box = {{"x1", runtime.visual.subject_box->x1},
               {"y1", runtime.visual.subject_box->y1},
               {"x2", runtime.visual.subject_box->x2},
               {"y2", runtime.visual.subject_box->y2}};
    }
    json keypoints = json::array();
    for (const auto& point : runtime.visual.keypoints) {
        keypoints.push_back({{"x", point.x}, {"y", point.y}, {"confidence", point.confidence}});
    }
    const auto current_phase = formal_session ? runtime.phase : session::Phase::Idle;
    const bool live_window = demo_status.mode == BoardDemoRuntime::Mode::LiveCamera;
    return {
        {"session", {
            {"id", runtime.context.session_id},
            {"phase", formal_session ? session::phase_name(runtime.phase) : "IDLE"},
            {"active", formal_session},
            {"target_duration_ms", runtime.context.target_duration_ms},
            {"elapsed_ms", runtime.elapsed_ms},
            {"remaining_ms", runtime.remaining_ms}
        }},
        {"source", {{"mode", boardDemoModeName(formal_session
                                                     ? BoardDemoRuntime::Mode::LiveCamera
                                                     : demo_status.mode)},
                    {"label", source_label},
                    {"formal", formal_session},
                    {"run_id", formal_session ? runtime.context.session_id
                                                : std::to_string(demo_status.run_id)},
                    {"media_timestamp_ms", demo_status.media_timestamp_ms},
                    {"clip", std::filesystem::path(demo_status.video_path)
                                 .filename().string()}}},
        {"health", {
            {"status", health_status},
            {"camera", formal_session && legacy.device.camera_ok},
            {"inference_fps", runtime.visual.inference_fps},
            {"inference_latency_ms", runtime.visual.inference_latency_ms},
            {"frame_age_ms", frame_age_ms},
            {"fallback_reason", demo_status.reason}
        }},
        {"visual", {
            {"presence", optionalNumber(runtime.visual.presence)},
            {"prone", optionalNumber(runtime.visual.prone)},
            {"frame_id", runtime.visual.frame_id},
            {"media_timestamp_ms", runtime.visual.media_timestamp_ms},
            {"subject_confidence", runtime.visual.subject_confidence},
            {"subject_box", box},
            {"keypoints", keypoints}
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
            {"available", runtime.gimbal.health.available},
            {"degraded", runtime.gimbal.health.degraded},
            {"health_status", runtime.gimbal.health.status},
            {"health_detail", runtime.gimbal.health.detail},
            {"angle_x", optionalNumber(runtime.gimbal.angle_x)},
            {"angle_y", optionalNumber(runtime.gimbal.angle_y)},
            {"tracking", runtime.gimbal.tracking},
            {"manual_lease", runtime.gimbal.manual_lease},
            {"lease_owner_id", runtime.gimbal.lease_owner_id},
            {"lease_epoch", runtime.gimbal.lease_epoch},
            {"lease_expires_at", runtime.gimbal.lease_expires_at_ms},
            {"search_generation", runtime.gimbal.search_generation},
            {"waypoint_index", runtime.gimbal.waypoint_index},
            {"subject_found", runtime.gimbal.subject_found},
            {"search_completed", runtime.gimbal.search_completed},
            {"protocol_version", runtime.gimbal.protocol_version},
            {"capabilities", runtime.gimbal.capabilities},
            {"link_health", runtime.gimbal.link_health},
            {"control_mode", runtime.gimbal.control_mode},
            {"target_yaw10", runtime.gimbal.target_yaw10
                                 ? json(*runtime.gimbal.target_yaw10) : json(nullptr)},
            {"target_pitch10", runtime.gimbal.target_pitch10
                                   ? json(*runtime.gimbal.target_pitch10) : json(nullptr)},
            {"motion_generation", runtime.gimbal.motion_generation
                                      ? json(*runtime.gimbal.motion_generation)
                                      : json(nullptr)},
            {"settled", runtime.gimbal.settled},
            {"calibration_valid", runtime.gimbal.calibration_valid},
            {"origin_valid", runtime.gimbal.origin_valid},
            {"fault_class", runtime.gimbal.fault_class},
            {"fault_code", runtime.gimbal.fault_code},
            {"recoverable", runtime.gimbal.recoverable},
            {"fault_latched_reason", runtime.gimbal.fault_latched_reason},
            {"detail", runtime.gimbal.detail}
        }},
        {"preview", {
            {"available", preview.available},
            {"sequence", preview.sequence},
            {"width", preview.width},
            {"height", preview.height},
            {"source_fps", preview.publish_fps},
            {"stream_fps", preview_stream_fps},
            {"capture_timestamp_ms", preview.timestamp_ms},
            {"encoded_sequence", encoded_preview_sequence},
            {"encoded_timestamp_ms", encoded_preview_timestamp_ms},
            {"encoded_age_ms", encoded_preview_age_ms},
            {"h264", {
                {"compiled", h264_preview.compiled},
                {"healthy", h264_preview.healthy},
                {"active", h264_preview.active},
                {"sequence", h264_sequence},
                {"age_ms", h264_age_ms},
                {"stream_fps", h264_preview.stream_fps},
                {"encode_ms", h264_preview.encode_ms},
                {"error", h264_preview.error},
                {"mime", "video/mp4; codecs=\"avc1.42E01E\""}
            }}
        }},
        {"controls", {
            {"can_switch_mode", !formal_session},
            {"phone_controls_enabled", !formal_session && demo_status.phone_controls_enabled},
            {"video_ready", demo_status.video_ready},
            {"replay_ready", demo_status.replay_ready},
            {"session", {
                {"start_enabled", !formal_session && live_window},
                {"confirm_enabled", formal_session && current_phase == session::Phase::Precheck},
                {"pause_enabled", formal_session && current_phase == session::Phase::Focusing},
                {"resume_enabled", formal_session && current_phase == session::Phase::Paused},
                {"end_enabled", formal_session},
                {"default_duration_minutes", clampInt(
                    legacy.preferences.default_focus_duration_min, 1, 240)},
                {"reason", !live_window && !formal_session
                               ? "live_mode_required"
                               : formal_session ? "session_active" : "ready_to_start"}
            }},
            {"gimbal", {
                 {"available", gimbal_precheck_ != nullptr},
                 {"manual_enabled", gimbal_manual_enabled},
                 {"manual_requires_lease", gimbal_focusing_phase},
                 {"skip_search_enabled", gimbal_skip_search_enabled},
                 {"takeover_enabled", gimbal_takeover_enabled},
                 {"release_enabled", gimbal_release_enabled},
                {"force_release_enabled", gimbal_force_release_enabled},
                {"recover_enabled", gimbal_recover_enabled},
                {"reason", gimbal_control_reason}
            }}
        }}
    };
}

DemoHttpServer::json DemoHttpServer::handleRuntimeSession(const json& data) {
    const auto action = lowerCopy(data.value("action", ""));
    if (action == "start") {
        if (sessions_.active()) {
            return {{"success", false}, {"error", "session_already_active"}};
        }
        if (demo_runtime_.status().mode != BoardDemoRuntime::Mode::LiveCamera) {
            return {{"success", false}, {"error", "live_mode_required"}};
        }
        if (!data.contains("duration_minutes") ||
            !data["duration_minutes"].is_number_integer()) {
            return {{"success", false}, {"error", "duration_minutes_required"}};
        }
        const int duration_minutes = data["duration_minutes"].get<int>();
        if (duration_minutes < 1 || duration_minutes > 240) {
            return {{"success", false}, {"error", "duration_minutes_out_of_range"}};
        }
        const auto result = sessions_.start({
            static_cast<std::int64_t>(duration_minutes) * 60 * 1000,
            session::SourceMode::LiveCamera,
            "web-" + std::to_string(nowMs())});
        return {{"success", result.success},
                {"already_active", result.already_active},
                {"session_id", result.context ? result.context->session_id : ""},
                {"phase", session::phase_name(sessions_.phase())},
                {"error", result.error}};
    }

    if (!sessions_.active()) {
        return {{"success", false}, {"error", "no_active_session"}};
    }
    const auto runtime = snapshots_.read(nowMs());
    if (!data.contains("session_id") || !data["session_id"].is_string() ||
        data["session_id"].get<std::string>() != runtime.context.session_id) {
        return {{"success", false}, {"error", "session_id_mismatch"}};
    }

    session::SessionOrchestrator::Result result;
    if (action == "confirm_precheck") {
        if (runtime.phase != session::Phase::Precheck) {
            return {{"success", false}, {"error", "confirm_requires_precheck"}};
        }
        if (gimbal_precheck_) {
            const auto gimbal = gimbal_precheck_->snapshot();
            if (gimbal.session_id == runtime.context.session_id &&
                (gimbal.manual_lease ||
                 gimbal.state == gimbal::PrecheckState::ManualAvailable)) {
                if (!data.contains("owner_id") || !data["owner_id"].is_string()) {
                    return {{"success", false}, {"error", "manual_control_identity_required"}};
                }
                if (gimbal.manual_lease &&
                    (!data.contains("lease_token") ||
                     !data["lease_token"].is_string())) {
                    return {{"success", false}, {"error", "manual_lease_identity_required"}};
                }
                const auto confirmed = gimbal_precheck_->confirmPosition(
                    gimbal::ManualCommandContext{
                        runtime.context.session_id,
                        data["owner_id"].get<std::string>(),
                        data.value("lease_token", ""),
                        data.value("sequence", gimbal.last_command_sequence + 1)},
                    session::Phase::Precheck, nowMs());
                if (!confirmed.success) {
                    return {{"success", false},
                            {"error", confirmed.error.empty()
                                          ? "gimbal_position_confirm_failed"
                                          : confirmed.error}};
                }
            }
        }
        result = sessions_.confirmReady();
    } else if (action == "pause") {
        result = sessions_.pause();
    } else if (action == "resume") {
        result = sessions_.resume();
    } else if (action == "end") {
        const bool formal_started = runtime.formal_active ||
            runtime.phase == session::Phase::Focusing ||
            runtime.phase == session::Phase::Paused;
        result = sessions_.end(formal_started ? session::StopReason::Completed
                                              : session::StopReason::Cancelled);
    } else {
        return {{"success", false}, {"error", "unknown_session_action"}};
    }

    return {{"success", result.success},
            {"session_id", result.context ? result.context->session_id : ""},
            {"phase", session::phase_name(sessions_.phase())},
            {"error", result.error}};
}

DemoHttpServer::json DemoHttpServer::handleDemoMode(const json& data) {
    if (sessions_.active()) {
        return {{"success", false}, {"error", "cannot switch display mode during a formal session"}};
    }
    const auto mode = lowerCopy(data.value("mode", "live"));
    if (mode == "live") {
        std::string error;
        const bool stopped = demo_runtime_.stop(&error);
        return {{"success", stopped}, {"mode", "LIVE_CAMERA"}, {"error", error}};
    }
    if (mode == "replay") {
        demo_runtime_.startFrontendReplay("forced_by_operator");
        const auto status = demo_runtime_.status();
        const bool started = status.mode == BoardDemoRuntime::Mode::FrontendReplay;
        return {{"success", started},
                {"mode", boardDemoModeName(status.mode)},
                {"error", started ? "" : "formal session owns the vision runtime"}};
    }
    if (mode != "recorded") {
        return {{"success", false},
                {"error", "mode must be live, recorded, or replay"}};
    }
    std::string error;
    const bool npu_started = demo_runtime_.startRecorded(&error);
    const auto status = demo_runtime_.status();
    return {{"success", npu_started || status.mode == BoardDemoRuntime::Mode::FrontendReplay},
            {"mode", boardDemoModeName(status.mode)},
            {"fallback", !npu_started},
            {"error", error}};
}

DemoHttpServer::json DemoHttpServer::handleRuntimePhone(const json& data) {
    if (sessions_.active()) {
        return {{"accepted", false}, {"error", "phone facts are read-only during a formal session"}};
    }
    const std::string category_text = data.value("category", "OFF");
    std::string error;
    const bool accepted = demo_runtime_.publishPhone(category_text, &error);
    return {{"accepted", accepted}, {"error", error}};
}

DemoHttpServer::json DemoHttpServer::handleRuntimeGimbal(const json& data) {
    if (!gimbal_precheck_) {
        return {{"accepted", false}, {"error", "gimbal_precheck_unavailable"}};
    }
    const auto phase = sessions_.phase();
    if (!sessions_.active() ||
        (phase != session::Phase::Precheck && phase != session::Phase::Focusing)) {
        return {{"accepted", false},
                {"error", "gimbal_control_requires_precheck_or_focusing"}};
    }

    const auto runtime = snapshots_.read(nowMs());
    const auto session_id = runtime.context.session_id;
    const auto before = gimbal_precheck_->snapshot();
    if (session_id.empty() || before.session_id != session_id) {
        return {{"accepted", false}, {"error", "session_mismatch"}};
    }

    const auto action = lowerCopy(data.value("action", ""));
    if (!data.contains("owner_id") || !data["owner_id"].is_string() ||
        !data.contains("sequence") || !data["sequence"].is_number_unsigned()) {
        return {{"accepted", false}, {"error", "owner_id_and_sequence_required"}};
    }
    const auto owner_id = data["owner_id"].get<std::string>();
    const auto sequence = data["sequence"].get<std::uint64_t>();
    const auto now_ms = nowMs();
    gimbal::CommandResult result;
    if (action == "skip") {
        if (phase != session::Phase::Precheck) {
            return {{"accepted", false},
                    {"error", "skip_search_requires_precheck"}};
        }
        result = gimbal_precheck_->skipAuto(session_id, sequence, now_ms);
    } else if (action == "takeover") {
        if (phase != session::Phase::Focusing) {
            return {{"accepted", false},
                    {"error", "takeover_requires_focusing"}};
        }
        result = gimbal_precheck_->manualTakeover(
            session_id, owner_id, sequence, phase, now_ms);
    } else if (action == "recover") {
        result = gimbal_precheck_->realign(session_id, sequence, now_ms);
    } else {
        const bool tokenless_operator_action = action == "force_release" ||
            (phase == session::Phase::Precheck &&
            (action == "up" || action == "down" || action == "left" ||
              action == "right" || action == "center" || action == "stop" ||
              action == "force_release"));
        if (!tokenless_operator_action &&
            (!data.contains("lease_token") || !data["lease_token"].is_string())) {
            return {{"accepted", false}, {"error", "lease_token_required"}};
        }
        const gimbal::ManualCommandContext lease{
            session_id, owner_id, data.value("lease_token", ""), sequence};
        if (data.contains("fine_tune") && !data["fine_tune"].is_boolean()) {
            return {{"accepted", false}, {"error", "fine_tune_must_be_boolean"}};
        }
        const bool fine_tune = data.value("fine_tune", false);
        if (action == "keepalive") {
            result = gimbal_precheck_->keepManualLease(lease, phase, now_ms);
        } else if (action == "release") {
            result = gimbal_precheck_->releaseManualControl(lease, phase, now_ms);
        } else if (action == "up") {
            result = gimbal_precheck_->nudge(
                lease, gimbal::NudgeDirection::Up, now_ms, fine_tune);
        } else if (action == "down") {
            result = gimbal_precheck_->nudge(
                lease, gimbal::NudgeDirection::Down, now_ms, fine_tune);
        } else if (action == "left") {
            result = gimbal_precheck_->nudge(
                lease, gimbal::NudgeDirection::Left, now_ms, fine_tune);
        } else if (action == "right") {
            result = gimbal_precheck_->nudge(
                lease, gimbal::NudgeDirection::Right, now_ms, fine_tune);
        } else if (action == "center") {
            result = gimbal_precheck_->center(lease, now_ms);
        } else if (action == "stop") {
            const bool auto_active = phase == session::Phase::Precheck &&
                (before.state == gimbal::PrecheckState::Searching ||
                 before.state == gimbal::PrecheckState::Centering ||
                 before.state == gimbal::PrecheckState::Framing ||
                 before.state == gimbal::PrecheckState::Holding);
            result = auto_active
                ? gimbal_precheck_->skipAuto(session_id, sequence, now_ms)
                : gimbal_precheck_->stopMotion(lease, now_ms);
        } else if (action == "force_release") {
            if (!data.value("confirmed", false)) {
                return {{"accepted", false}, {"error", "force_release_confirmation_required"}};
            }
            result = gimbal_precheck_->forceReleaseOperator(
                session_id, owner_id, sequence, now_ms);
        } else {
            return {{"accepted", false}, {"error", "unknown_gimbal_action"}};
        }
    }

    const auto after = gimbal_precheck_->snapshot();
    if (!result.success) {
        return {{"accepted", false},
                {"error", result.error.empty() ? "gimbal_command_failed" : result.error},
                {"state", gimbal::precheckStateName(after.state)},
                {"manual_lease", after.manual_lease},
                {"lease_owner_id", after.lease_owner_id},
                {"sequence", sequence}};
    }
    return {{"accepted", true},
            {"phase", session::phase_name(phase)},
            {"state", gimbal::precheckStateName(after.state)},
            {"manual_lease", after.manual_lease},
            {"lease_owner_id", after.lease_owner_id},
            {"lease_token", after.lease_token},
            {"lease_epoch", after.lease_epoch},
            {"lease_expires_at", after.lease_expires_at_ms},
            {"fine_tune", result.fine_tune},
            {"resolved_step_deg", result.resolved_step_deg},
            {"motion_status", result.motion_status},
            {"boundary_stop_planned", result.boundary_stop_planned},
            {"boundary_paused", result.boundary_paused},
            {"detail", after.detail},
            {"sequence", sequence}};
}

std::string DemoHttpServer::previewFrameResponse() const {
#ifdef EDGEFOCUS_WITH_OPENCV
    const auto selected = previewSourceSelection();
    std::shared_ptr<const EncodedPreviewFrame> encoded;
    {
        std::lock_guard<std::mutex> lock(encoded_preview_mutex_);
        if (encoded_preview_ && encoded_preview_->source_key == selected.key) {
            encoded = encoded_preview_;
        }
    }
    if (!encoded || encoded->jpeg.empty()) {
        return httpResponse(503, json{{"error", "preview frame unavailable"}}.dump());
    }
    return httpResponse(200,
                        std::string(reinterpret_cast<const char*>(encoded->jpeg.data()),
                                    encoded->jpeg.size()),
                        "image/jpeg");
#else
    return httpResponse(503, json{{"error", "OpenCV preview not compiled"}}.dump());
#endif
}

std::string DemoHttpServer::recordedVideoResponse() const {
    const auto status = demo_runtime_.status();
    std::ifstream input(status.video_path, std::ios::binary);
    if (!input) {
        return httpResponse(404, json{{"error", "recorded video unavailable"}}.dump());
    }
    std::ostringstream bytes;
    bytes << input.rdbuf();
    return httpResponse(200, bytes.str(), "video/mp4");
}

std::string DemoHttpServer::replayTraceResponse() const {
    const auto status = demo_runtime_.status();
    auto trace_path = std::filesystem::path(status.video_path);
    trace_path.replace_extension(".trace.json");
    std::ifstream input(trace_path, std::ios::binary);
    if (!input) {
        return httpResponse(404,
            json{{"error", "pre-log trace unavailable"},
                 {"clip", trace_path.filename().string()}}.dump());
    }
    std::ostringstream bytes;
    bytes << input.rdbuf();
    return httpResponse(200, bytes.str(), "application/json; charset=utf-8");
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
            {"external_power_state", snap.device.external_power_state},
            {"external_power_present", externalPowerPresentWire(snap.device)},
            {"external_power_sample_age_ms", externalPowerSampleAgeWire(snap.device)},
            {"power_profile", powerProfileWire(snap.device.power_profile)},
            {"power_profile_source", snap.device.power_profile_source},
            {"manual_override_active", snap.device.manual_override_active},
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
    if (!gimbal_monitor_ || !store_.snapshot().preferences.gimbal_enabled) {
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

    auto result = gimbal_monitor_->executeBusinessCommand(
        "SET XY " + std::to_string(x) + " " + std::to_string(y), 15000);
    return json{{"status", "success"}, {"data", {{"success", result.success},
                                                  {"command", result.command},
                                                  {"final_response", result.final_response},
                                                  {"error", result.error}}}};
}

} // namespace edgefocus
