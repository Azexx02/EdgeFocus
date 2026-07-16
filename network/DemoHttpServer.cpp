#include "DemoHttpServer.h"
#include "demo/BoardDemoRuntime.h"

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
    return R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>EdgeFocus 视觉推理链</title><link rel="icon" href="data:,"><style>
:root{--bg:#071018;--panel:#0b1621;--line:#203343;--ink:#e7f3f4;--muted:#78909d;--cyan:#49d6ff;--green:#5ee6a8;--amber:#ffca61;--red:#ff625f;--violet:#9f83ff}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 15% 0,#102737 0,transparent 38%),var(--bg);color:var(--ink);font-family:"Microsoft YaHei UI",sans-serif;min-height:100vh;overflow:hidden}body:before{content:"";position:fixed;inset:0;pointer-events:none;opacity:.18;background-image:linear-gradient(#5f82920d 1px,transparent 1px),linear-gradient(90deg,#5f82920d 1px,transparent 1px);background-size:24px 24px}
header{height:68px;padding:0 22px;border-bottom:1px solid var(--line);display:flex;align-items:center;justify-content:space-between;background:#071018e8}.brand{display:flex;gap:13px;align-items:center}.mark{width:27px;height:34px;display:flex;align-items:end;gap:3px}.mark i{display:block;width:7px;background:var(--cyan)}.mark i:nth-child(1){height:15px}.mark i:nth-child(2){height:30px}.mark i:nth-child(3){height:22px}.eyebrow{font:700 9px/1.2 Consolas;letter-spacing:2px;color:var(--cyan)}.brand h1{font:600 22px/1 "Bahnschrift Condensed","Microsoft YaHei UI";margin:4px 0 0}.brand h1 span{font-weight:300;color:#91a5ae}.metrics{display:flex;align-items:center;gap:24px}.metric{border-left:1px solid var(--line);padding-left:16px}.metric small{display:block;font:9px Consolas;color:var(--muted)}.metric b{font:700 13px Consolas}.badge{padding:7px 11px;border:1px solid #285c48;color:var(--green);font:700 10px Consolas}.badge.warn{border-color:#694d26;color:var(--amber)}.badge.replay{border-color:#674c99;color:#bca8ff}
main{height:calc(100vh - 68px);display:grid;grid-template-columns:minmax(0,1.78fr) minmax(350px,.82fr);grid-template-rows:minmax(0,1fr) 150px;gap:9px;padding:9px}.panel{background:#0a141ed9;border:1px solid var(--line);min-width:0}.stage{padding:10px;display:flex;min-height:0;flex-direction:column}.panel-head{height:32px;display:flex;align-items:center;justify-content:space-between;font:700 10px Consolas;letter-spacing:1px;color:#9ab0bb}.index{color:var(--cyan);margin-right:10px}.source{color:var(--green)}
.video{position:relative;flex:1;min-height:300px;background:#020609;display:grid;place-items:center;overflow:hidden;border:1px solid #263b49}.video img,.video video{display:none;width:100%;height:100%;object-fit:contain}.empty{position:absolute;color:#526977;font:12px Consolas;letter-spacing:1px}.scan{position:absolute;inset:0;background:linear-gradient(transparent 49.7%,#49d6ff18 50%,transparent 50.3%);background-size:100% 9px;pointer-events:none}.overlay{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}.hud{position:absolute;left:14px;top:13px;font:700 12px Consolas;text-shadow:0 1px 4px #000}.hud b{color:var(--cyan)}.stamp{position:absolute;bottom:12px;left:12px;display:flex;gap:8px}.stamp span{background:#071018e8;border:1px solid var(--line);padding:5px 8px;font:9px Consolas;color:var(--green)}
.toolbar{display:flex;align-items:center;gap:7px;padding-top:9px;min-height:48px}.mode-btn,.phone-buttons button{cursor:pointer;border:1px solid #315063;background:#0b1721;color:#9eb1ba;padding:7px 10px;font:10px "Microsoft YaHei UI"}.mode-btn:hover,.phone-buttons button:hover{border-color:var(--cyan);color:#fff}.mode-btn.active,.phone-buttons button.selected{border-color:var(--green);color:var(--green);background:#0c241c}.mode-btn[disabled],.phone-buttons button[disabled]{cursor:not-allowed;opacity:.38}.switch{display:flex;gap:6px;align-items:center;margin-left:8px;color:#718893;font:9px Consolas}.note{margin-left:auto;color:#5f7682;font:9px Consolas;text-align:right}
aside{display:grid;grid-template-columns:1fr 1fr;grid-template-rows:auto auto;gap:8px;min-height:0}.evidence{position:relative;padding:15px 16px;border-top:2px solid var(--green);min-height:0}.evidence.amber{border-color:var(--amber)}.evidence.violet{border-color:var(--violet)}.evidence.red{border-color:var(--red)}.evidence .code{position:absolute;right:12px;top:9px;font:25px Georgia;color:#314755}.evidence p{margin:0 0 7px;font:9px Consolas;letter-spacing:1.3px;color:#78909d}.evidence h2{margin:0 0 5px;font:600 20px "Bahnschrift Condensed","Microsoft YaHei UI"}.evidence small{font-size:10px;color:#718893}.score{position:absolute;right:14px;bottom:13px;font:700 14px Consolas;color:var(--green)}.phone-buttons{display:flex;gap:4px;flex-wrap:wrap;margin-top:9px}.phone-buttons button{padding:6px 8px}.level{display:flex;gap:12px;align-items:center}.orb{width:49px;height:49px;border:1px solid #365064;display:grid;place-items:center;font:700 27px Consolas;color:var(--red)}.reason{font:9px Consolas;color:#9aaeb5;word-break:break-all}.truth{margin-top:10px;padding-top:8px;border-top:1px solid #1c303d;font:9px Consolas;color:#607783}
.timeline{grid-column:1/3;padding:8px 12px 10px;display:flex;flex-direction:column}.timeline .panel-head{height:27px}.legend{display:flex;gap:14px;font:9px Consolas}.legend i{display:inline-block;width:10px;height:2px;margin-right:5px;vertical-align:middle}.timeline canvas{width:100%;height:100px;display:block}
@media(max-width:980px){body{overflow:auto}main{height:auto;grid-template-columns:1fr;grid-template-rows:auto}.timeline{grid-column:1}.video{height:55vh}.metrics .metric{display:none}aside{grid-template-columns:1fr 1fr}}
</style></head><body>
<header><div class="brand"><div class="mark"><i></i><i></i><i></i></div><div><div class="eyebrow">EDGE AI · LIVE EVIDENCE</div><h1>EdgeFocus <span>视觉推理链</span></h1></div></div><div class="metrics"><div class="metric"><small>PREVIEW</small><b id="preview-fps">-- FPS</b></div><div class="metric"><small>NPU INFERENCE</small><b id="npu-metric">-- FPS · -- ms</b></div><div id="health" class="badge warn">IDLE</div></div></header>
<main><section class="panel stage"><div class="panel-head"><span><b class="index">01</b>视觉输入与证据叠加</span><span id="source" class="source">LIVE CAMERA</span></div><div class="video"><div id="empty" class="empty">等待 App 启动正式会话，或选择预录演示</div><img id="frame" alt="板端视觉输入"><video id="replay" muted playsinline preload="auto"></video><div class="scan"></div><svg id="overlay" class="overlay" viewBox="0 0 1000 1000" preserveAspectRatio="none"><rect id="subject-box" fill="none" stroke="#ffca61" stroke-width="4" visibility="hidden"/><g id="skeleton"></g></svg><div class="hud"><span id="runtime-kind">FORMAL SESSION</span><br>RUN <b id="run-id">--</b><br>FRAME <b id="frame-id">--</b></div><div class="stamp"><span id="phase">IDLE</span><span id="frame-age">NO EVIDENCE</span></div></div><div class="toolbar"><button id="mode-live" class="mode-btn active">现场摄像头</button><button id="mode-recorded" class="mode-btn">预录 NPU</button><button id="mode-replay" class="mode-btn">纯前端演示</button><label class="switch"><input id="show-overlay" type="checkbox" checked>显示证据叠加</label><span id="mode-note" class="note">现场模式只读，正式会话由 App 启动</span></div></section>
<aside><section class="panel evidence"><span class="code">A</span><p>VISUAL · PRESENCE</p><h2 id="presence">等待证据</h2><small id="presence-detail">会话外不运行现场 NPU</small><b id="presence-score" class="score">--</b></section><section class="panel evidence amber"><span class="code">B</span><p>TEMPORAL MLP · PRONE</p><h2 id="prone">等待时序窗口</h2><small>时序模型持续确认，单帧不直接触发</small><b id="prone-score" class="score">--</b></section><section class="panel evidence violet"><span class="code">C</span><p>EXTERNAL FACT · PHONE</p><h2 id="phone">未使用</h2><small id="phone-source">现场模式由 App 蓝牙同步</small><div class="phone-buttons"><button data-phone="OFF">未使用</button><button data-phone="LEARNING">学习</button><button data-phone="ENTERTAINMENT">娱乐</button><button data-phone="GENERAL_OR_UNKNOWN">其他</button></div></section><section class="panel evidence red"><span class="code">D</span><p>FUSED DISTRACTION</p><div class="level"><div id="level" class="orb">—</div><div><h2 id="level-title">当前无分神等级</h2><div id="reasons" class="reason">UNKNOWN</div></div></div><div id="delivery" class="truth">正式会话：等待 App/手环投递状态</div></section></aside>
<section class="panel timeline"><div class="panel-head"><span><b class="index">02</b>近 30 秒证据时间线</span><span class="legend"><span><i style="background:#5ee6a8"></i>人在</span><span><i style="background:#ffca61"></i>趴下</span><span><i style="background:#9f83ff"></i>手机上报</span><span><i style="background:#ff625f"></i>综合等级</span></span></div><canvas id="timeline"></canvas></section></main>
<script>
const $=id=>document.getElementById(id), frame=$('frame'), replay=$('replay'), overlay=$('overlay');let previewFrames=0,previewAt=performance.now(),previewEnabled=false,frameBusy=false,lastRun='',lastSampleAt=0,history=[],lastState=null,replayTrace=null,replayTraceClip='',replayTraceLoading='',replayVideoClip='',replayAdvanceBusy=false;
async function api(url,opt){const r=await fetch(url,opt),x=await r.json();if(!r.ok)throw Error(x.error||r.status);return x}const post=(url,data)=>api(url,{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(data)});const pct=v=>v==null?'--':Math.round(v*100)+'%';
function stopPreview(){previewEnabled=false;frameBusy=false;frame.style.display='none';frame.removeAttribute('src')}
function unloadReplay(){replay.pause();replay.style.display='none';replayVideoClip='';replayAdvanceBusy=false;if(replay.getAttribute('src')){replay.removeAttribute('src');replay.load()}}
function requestReplayTrace(clip){if(!clip||replayTraceClip===clip||replayTraceLoading===clip)return;replayTraceLoading=clip;api('/api/demo/trace.json?clip='+encodeURIComponent(clip)).then(trace=>{if(replayTraceLoading!==clip)return;replayTrace=trace;replayTraceClip=clip;replayTraceLoading=''}).catch(()=>{if(replayTraceLoading===clip){replayTrace=null;replayTraceClip=clip;replayTraceLoading=''}})}
function scriptedState(s){if(s.source.mode!=='FRONTEND_REPLAY')return s;const clip=s.source.clip||'';requestReplayTrace(clip);if(!replay.duration||replayTraceClip!==clip||!replayTrace?.samples?.length)return s;const t=Math.round(replay.currentTime*1000),samples=replayTrace.samples;let sample=samples[0];for(const item of samples){if(item.media_timestamp_ms>t)break;sample=item}const x=structuredClone(s);x.source.media_timestamp_ms=t;x.visual=structuredClone(sample.visual);x.visual.media_timestamp_ms=t;x.phone=structuredClone(sample.phone);x.assessment=structuredClone(sample.assessment);return x}
function drawOverlay(v){const box=$('subject-box'),g=$('skeleton');g.replaceChildren();if(!$('show-overlay').checked||!v.subject_box){box.setAttribute('visibility','hidden');return}const b=v.subject_box;box.setAttribute('x',b.x1*1000);box.setAttribute('y',b.y1*1000);box.setAttribute('width',(b.x2-b.x1)*1000);box.setAttribute('height',(b.y2-b.y1)*1000);box.setAttribute('visibility','visible');const pts=v.keypoints||[],links=[[5,6],[5,7],[7,9],[6,8],[8,10],[5,11],[6,12],[11,12],[11,13],[13,15],[12,14],[14,16],[0,5],[0,6]];for(const [a,bx] of links){if(pts[a]?.confidence>.25&&pts[bx]?.confidence>.25){const l=document.createElementNS('http://www.w3.org/2000/svg','line');for(const [k,val] of [['x1',pts[a].x*1000],['y1',pts[a].y*1000],['x2',pts[bx].x*1000],['y2',pts[bx].y*1000]])l.setAttribute(k,val);l.setAttribute('stroke','#49d6ff');l.setAttribute('stroke-width','4');g.append(l)}}pts.forEach(p=>{if(p.confidence<=.25)return;const c=document.createElementNS('http://www.w3.org/2000/svg','circle');c.setAttribute('cx',p.x*1000);c.setAttribute('cy',p.y*1000);c.setAttribute('r','6');c.setAttribute('fill','#49d6ff');g.append(c)})}
function sample(s){const now=performance.now();if(now-lastSampleAt<240)return;lastSampleAt=now;const run=s.source.run_id;if(run!==lastRun){history=[];lastRun=run}history.push({t:Date.now(),p:s.visual.presence,q:s.visual.prone,phone:s.phone.active?1:0,l:s.assessment.level});const cutoff=Date.now()-30000;history=history.filter(x=>x.t>=cutoff);drawTimeline()}
function drawTimeline(){const c=$('timeline'),d=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;c.width=w*d;c.height=h*d;const x=c.getContext('2d');x.scale(d,d);x.clearRect(0,0,w,h);const rows=[['PRES','#5ee6a8','p'],['PRONE','#ffca61','q'],['PHONE','#9f83ff','phone'],['LEVEL','#ff625f','l']],left=44,rowH=h/4,now=Date.now();x.font='9px Consolas';rows.forEach((r,i)=>{const y=i*rowH+rowH/2;x.fillStyle='#607783';x.fillText(r[0],2,y+3);x.strokeStyle='#203343';x.beginPath();x.moveTo(left,y);x.lineTo(w,y);x.stroke();x.strokeStyle=r[1];x.lineWidth=2;x.beginPath();let started=false;history.forEach(v=>{const val=v[r[2]];if(val==null){started=false;return}const px=left+(w-left)*(1-(now-v.t)/30000),py=y-(r[2]==='l'?Math.min(3,val)/3:Math.max(0,Math.min(1,val)))*(rowH*.34);if(!started){x.moveTo(px,py);started=true}else x.lineTo(px,py)});x.stroke()})}
function render(raw){let s=scriptedState(raw);lastState=s;const replayMode=s.source.mode==='FRONTEND_REPLAY',liveMode=s.source.mode==='LIVE_CAMERA',recordedMode=s.source.mode==='RECORDED_NPU';$('run-id').textContent=String(s.source.run_id||'--').slice(-12);$('runtime-kind').textContent=s.source.formal?'FORMAL SESSION':liveMode?'FORMAL SESSION WINDOW':replayMode?'FRONTEND REPLAY':'DEMO RUNTIME';$('phase').textContent=s.session.phase;$('source').textContent=s.source.label;$('frame-id').textContent=s.visual.frame_id||'--';$('npu-metric').textContent=replayMode?'NPU BYPASSED':`${Number(s.health.inference_fps||0).toFixed(1)} FPS · ${Number(s.health.inference_latency_ms||0).toFixed(0)} ms`;$('health').textContent=s.health.status;$('health').className='badge '+(s.health.status==='RUNNING'?'':replayMode?'replay':'warn');
const p=s.visual.presence,q=s.visual.prone,l=s.assessment.level;$('presence').textContent=p==null?'证据不足':p>=.5?'人在':'人不在';$('presence-score').textContent=pct(p);$('presence-detail').textContent=s.health.frame_age_ms<0&&!replayMode?'等待视觉证据':s.assessment.applicability==='NOT_PRESENT'?'等级不适用':replayMode?'同步展示轨迹':`证据年龄 ${s.health.frame_age_ms} ms`;$('prone').textContent=q==null?'等待时序窗口':q>=.5?'已确认趴下':'未趴下';$('prone-score').textContent=pct(q);$('phone').textContent=s.phone.active?({LEARNING:'学习类使用',ENTERTAINMENT:'娱乐类使用',GENERAL_OR_UNKNOWN:'其他使用'}[s.phone.category]||'使用中'):'未使用';$('phone-source').textContent=s.source.formal?'来自 App 蓝牙原始事实':liveMode?'现场模式只读，等待 App 会话':replayMode?'由同步回放轨迹驱动':'本地演示输入';$('level').textContent=l==null?'—':l;$('level-title').textContent=l==null?(s.assessment.applicability==='NOT_PRESENT'?'人不在 · 等级不适用':'当前无分神等级'):["未分神","轻度分神","持续分神","严重 / 长时分神"][l];$('reasons').textContent=(s.assessment.reasons||[]).join(' + ')||s.assessment.applicability;
document.querySelectorAll('[data-phone]').forEach(b=>{b.disabled=!s.controls.phone_controls_enabled||replayMode;b.classList.toggle('selected',(s.phone.active?b.dataset.phone===s.phone.category:b.dataset.phone==='OFF'))});$('mode-live').disabled=!s.controls.can_switch_mode;$('mode-recorded').disabled=!s.controls.can_switch_mode||!s.controls.video_ready;$('mode-replay').disabled=!s.controls.can_switch_mode||!s.controls.replay_ready;$('mode-live').classList.toggle('active',liveMode);$('mode-recorded').classList.toggle('active',recordedMode);$('mode-replay').classList.toggle('active',replayMode);$('mode-note').textContent=s.source.formal?'正式会话运行中，模式已锁定':liveMode?'现场模式只读，正式会话由 App 启动':replayMode?'纯前端 pre-log 回放 · 手机输入已锁定':'预录 NPU 模式不写正式会话或提醒';
if(replayMode){stopPreview();const clip=s.source.clip||'';if(replayVideoClip!==clip||!replay.getAttribute('src')){replay.pause();if(replay.getAttribute('src')){replay.removeAttribute('src');replay.load()}replayVideoClip=clip;replay.src='/api/demo/recorded.mp4?clip='+encodeURIComponent(clip);replay.play().catch(()=>{})}replay.style.display='block';$('empty').style.display='none'}else{unloadReplay();previewEnabled=!!s.preview.available;if(previewEnabled){$('empty').style.display='none'}else{stopPreview();$('empty').style.display='block'}}$('frame-age').textContent=replayMode?`MEDIA ${Math.round(replay.currentTime||0)} s`:s.health.frame_age_ms>=0?`EVIDENCE ${s.health.frame_age_ms} ms OLD`:'NO EVIDENCE';drawOverlay(s.visual);sample(s)}
frame.addEventListener('load',function(){frameBusy=false;this.style.display=previewEnabled?'block':'none';if(!previewEnabled)return;previewFrames++;const now=performance.now();if(now-previewAt>1000){$('preview-fps').textContent=(previewFrames*1000/(now-previewAt)).toFixed(1)+' FPS';previewFrames=0;previewAt=now}});frame.addEventListener('error',()=>{frameBusy=false});replay.addEventListener('timeupdate',()=>{if(lastState?.source.mode==='FRONTEND_REPLAY')render(lastState)});replay.addEventListener('ended',()=>{if(lastState?.source.mode!=='FRONTEND_REPLAY'||replayAdvanceBusy)return;replayAdvanceBusy=true;history=[];post('/api/demo/mode',{mode:'replay'}).catch(()=>{}).finally(()=>{replayAdvanceBusy=false})});
$('mode-live').onclick=()=>post('/api/demo/mode',{mode:'live'});$('mode-recorded').onclick=()=>post('/api/demo/mode',{mode:'recorded'});$('mode-replay').onclick=()=>post('/api/demo/mode',{mode:'replay'});$('show-overlay').onchange=()=>lastState&&drawOverlay(lastState.visual);document.querySelectorAll('[data-phone]').forEach(b=>b.onclick=()=>post('/api/demo/phone',{category:b.dataset.phone}));
function refreshPreview(){if(!previewEnabled||frameBusy)return;frameBusy=true;frame.src=`/api/demo/frame.jpg?t=${Date.now()}`}
async function poll(){try{render(await api('/api/demo/state'))}catch(e){$('health').textContent='OFFLINE';$('health').className='badge warn'}}poll();setInterval(poll,180);setInterval(refreshPreview,100);addEventListener('resize',drawTimeline);
</script></body></html>)HTML";
}
} // namespace

DemoHttpServer::DemoHttpServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                               DeviceManager& device_mgr, GimbalMonitor* gimbal_monitor,
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
    const bool formal_session = sessions_.active();
    const auto demo_status = demo_runtime_.status();
    const auto runtime = formal_session ? snapshots_.read() : demo_runtime_.snapshot();
    const auto legacy = store_.snapshot();
    const auto preview = formal_session ? preview_frames_.read() : demo_runtime_.previewFrames().read();
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
    return {
        {"session", {
            {"id", runtime.context.session_id},
            {"phase", formal_session ? session::phase_name(runtime.phase) : "IDLE"},
            {"active", formal_session},
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
            {"search_generation", runtime.gimbal.search_generation},
            {"waypoint_index", runtime.gimbal.waypoint_index},
            {"subject_found", runtime.gimbal.subject_found},
            {"recoverable", runtime.gimbal.recoverable},
            {"fault_latched_reason", runtime.gimbal.fault_latched_reason},
            {"detail", runtime.gimbal.detail}
        }},
        {"preview", {
            {"available", preview.has_value()},
            {"sequence", preview ? preview->sequence : 0}
        }},
        {"controls", {
            {"can_switch_mode", !formal_session},
            {"phone_controls_enabled", !formal_session && demo_status.phone_controls_enabled},
            {"video_ready", demo_status.video_ready},
            {"replay_ready", demo_status.replay_ready}
        }}
    };
}

DemoHttpServer::json DemoHttpServer::handleRuntimeSession(const json& data) {
    (void)data;
    return {{"success", false},
            {"error", "formal sessions are controlled by the App, not the display page"}};
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

std::string DemoHttpServer::previewFrameResponse() const {
    const bool formal_session = sessions_.active();
    const auto preview = formal_session ? preview_frames_.read() : demo_runtime_.previewFrames().read();
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
    const auto runtime = formal_session ? snapshots_.read() : demo_runtime_.snapshot();
    const std::string overlay = std::string(formal_session ? "LIVE CAMERA  |  NPU "
                                                            : "RECORDED INPUT  |  NPU ") +
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
