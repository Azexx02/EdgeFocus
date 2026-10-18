# EdgeFocus Agent 快速入口

本文件适用于当前主机目标仓库 `code/focus_app_national`。`code/focus_app` 是较早现场/原型树，除非用户明确要求回退或对照，不要把它当作默认修改目标。

开始修改前，先读
[`docs/architecture/current-architecture.md`](docs/architecture/current-architecture.md)。
它描述当前工作树实际架构；`docs/superpowers/specs/` 是历史设计依据，不一定等于当前实现。

相关仓库入口：

- G431 云台固件当前目标：`C:/Users/Aze/Desktop/self_developed/code/G431_national/`
- App 当前目标：`C:/Users/Aze/Desktop/APP/EdgeFocusApp/`
- App 同步交接入口：`C:/Users/Aze/Desktop/APP/新app迭代必看/`
- 总工作区入口：`C:/Users/Aze/Desktop/self_developed/AGENTS.md`

## 先建立这张心智图

- `AppContext` 管理进程级常驻模块和线程。
- `SessionOrchestrator` 是正式会话生命周期的唯一所有者。
- 每次正式会话创建新的 `SessionRuntime`；当前挂载
  `LegacySessionProjectionFeature` 和 `DistractionPipelineFeature`。
- 正式判定真值来自新 Presence/Prone/Phone Fusion 核心，并发布到
  `SessionSnapshotPublisher`。
- `SystemStateStore` 仍是相机、提醒、设备和旧 App 字段的兼容投影面；不要向它新增判定语义。
- `BoardDemoRuntime` 是无正式会话时的独立演示运行域，拥有自己的队列、手机事实、快照和推理实例。
- `PowerSourceService` 只读 STUSB4500 外部供电事实；`WorkloadProfilePolicy` 产生 Home/Go，有电默认 Home、无电默认 Go，手动覆盖到下一次确认的供电转换。
- `GimbalHardwareService` 是条件创建的进程级硬件所有者；`GimbalSessionFeature` 是正式会话 optional feature，只在已实测配置完整、未设置紧急禁用且运行时健康门通过时出现。
- 手环与 App 共用 BlueZ 适配器；当前 `hci0` 未声明 `central-peripheral` 并发角色，实测手机连接时手环连接超时、手机断开后手环可连接。因此手环自动连接默认关闭，只在展示页显式开启；正式产品优先保证 App，当前硬件不得假设 App/手环可同时连接。
- 手环运行时恢复只允许取消目标 `Device1` 连接、等待 discovery 收敛和 10/20/30 秒退避；禁止由手环状态机重启 `bluetooth.service`、复位/掉电 `hci0` 或全局移除设备，以免打断 App。
- 风扇是独立板级平台服务：Linux bridge 只上报 `cpu-thermal`，M33 独占 PWM/tach；它不属于正式会话或 Demo。

## 不可破坏的边界

1. 正式会话只能由 `SessionOrchestrator` 创建，且只允许 `LIVE_CAMERA`。
2. 正式会话与 Demo 互斥；正式会话优先取得相机/NPU所有权。
3. Demo 不写正式统计，不向 App/手环投递真实提醒。
4. 手机 App 只上报原始 `未使用/学习/娱乐/其他` 事实，不上报最终分神等级。
5. `NOT_PRESENT`、`UNKNOWN` 不是分神等级；只有 `ACTIVE` 才允许 level 0～3。
6. 当前视觉产品字段只有 Presence、Prone、主体框/关键点和质量数据。不要恢复困倦、闭眼疲劳、旧左右转头或视觉手机识别占位逻辑。
7. 板载屏幕和板载声音禁用。云台不再使用普通 feature opt-in；`motion_verified=false` 或机械参数不完整时必须自动拒绝启用，`EDGEFOCUS_DISABLE_GIMBAL=1` 仅作紧急维修开关。板子修复后仍须实测 UART、PD6、中心、方向和限位。
8. 浏览器展示页是只读证据面；现场模式不能手动改手机事实或伪造正式状态。
9. 纯前端回放必须明确标记 `NPU BYPASSED`，并使用板端正常 NPU 预跑生成的 pre-log；不得手填视觉结果。
10. 新硬件能力先确定进程级/会话级所有权和故障边界，再注册模块，禁止直接塞入 `main.cpp` 或 `SystemStateStore`。
11. 风扇固件、PB10/PB11 板级适配和 Linux 服务链已部署并启用开机链；用户在转速、回退和硬件默认自转通过后明确豁免接口波形截图。校准文件必须如实区分 measured 与 waived，禁止把豁免写成已测，禁止让 Linux 与 M33 同时驱动 PB10。
12. Home/Go 只调整应用层视觉采样节奏：Home 约 4 FPS、Go 约 3 FPS。不得降低 PD6 Fault、供电检测、BLE 心跳或 G431 内部控制频率；worker 必须优先处理最新帧。
13. 电量硬件已删除。禁止恢复 `battery_*`、旧 `power_source` 或伪造百分比；App 只使用 `external_power_*`、`power_profile` 和显式 `set_power_profile`。
14. 云台 PRECHECK 手动命令必须绑定当前 session、严格递增 sequence 和 manual lease。UART、PD6、对齐或控制器状态任一失败，PRECHECK 和会话内跟踪能力必须同时停用；只允许低频安全探测，连续健康后才可重新对齐，并按当前 phase 恢复搜索或 HOLD。
15. App 产品端云台控制只存在于 PRECHECK；正式会话中的浏览器云台控制是展示/诊断恢复能力，不是 App 产品需求。
16. App 端“停止云台”语义是强制释放/停止云台控制，不需要 `stop_enabled` 之类字段协商；主机若保留旧字段，应删除或停止对 App 暴露。

## 阅读顺序

1. `docs/architecture/current-architecture.md`
2. `main.cpp` 与 `CMakeLists.txt`
3. `session/SessionOrchestrator.*`、`session/SessionRuntime.*`
4. `vision/DistractionPipelineFeature.*`、`behavior/PhoneFactBuffer.*`
5. `session/SessionSnapshotPublisher.*`、`session/SessionDomain.h`
6. `network/BleGattServer.*`、`network/AppCommandService.*`
7. `policy/ReminderPolicy.*`、`device/DeviceManager.*`
8. 涉及演示时再读 `demo/BoardDemoRuntime.*`、`network/DemoHttpServer.*`
9. 涉及风扇/M33 时读 `docs/hardware/stm32mp257-m33-fan-inventory.md`，再读 `platform/fan/` 与 `firmware/m33/fan_controller/`
10. 涉及供电/Home-Go 时读 `platform/power/`、`vision/VisionCadencePolicy.h` 和 `docs/integration/app-update-handoff-2026-07-15.md`
11. 涉及云台时先读 `docs/hardware/g431-bringup-results-2026-07-15.md` 与已批准规格，再读 `platform/gimbal/`、`device/GimbalPrecheckController.*`

## 目录职责

| 目录 | 当前职责 |
|---|---|
| `session/` | 正式会话状态机、运行域、快照与兼容会话投影 |
| `core/distraction/` | 与 UI、BLE、会话无关的视觉/时序/Fusion 静态库 |
| `vision/` | STAI Pose、Prone MLP 与会话特性的适配层 |
| `camera/` | 唯一现场摄像头采集和共享预览帧 |
| `behavior/` | 手机事实缓冲和新 assessment 适配；旧行为代码不进入生产目标 |
| `network/` | App GATT Server、命令兼容门面、可靠事件 outbox、Demo HTTP |
| `policy/` | 当前提醒兼容策略与渠道路由 |
| `device/` | 手环 GATT Client、设备动作和保留但禁用的云台/屏幕/声音驱动 |
| `demo/` | 独立预录 NPU / 前端 pre-log 演示运行域 |
| `storage/` | SQLite 会话、事件和报告持久化 |
| `assets/demo/` | pNN NPU 素材、rNN 前端视频和同名真实 pre-log |
| `platform/fan/` | 与主业务隔离的温控核心、RPMsg bridge、资源切换/健康检查/回退服务 |
| `firmware/m33/fan_controller/` | 可复现构建并已上板的 M33 控制任务与 ATK HAL/OpenAMP 适配 |
| `platform/power/` | STUSB4500 只读检测、供电事实去抖与 Home/Go 有效策略 |
| `platform/gimbal/` | UART/GPIO 硬件状态机、协议解析和独立 bring-up 工具 |
| `device/GimbalPrecheckController.*` | PRECHECK 搜索、手动租约、session/sequence 门禁与故障降级 |

## 修改前检查

- 这项状态属于进程、正式会话、Demo，还是某个外设？只能有一个所有者。
- 数据是否带 `session_id/sequence/timestamp/freshness`？
- 是否绕过了 `SessionOrchestrator` 或重新引入第二个相机/NPU所有者？
- 是否把新判定写回了 `risk_score/FocusState` 而没有明确兼容边界？
- 失败时应终止正式会话，还是只降级某个可选能力？
- 是否更新本架构文档、测试和板端部署说明？

## 常用验证入口

```powershell
# 工作区检查（仓库所有权不同，必须显式声明 safe.directory）
git -c safe.directory=C:/Users/Aze/Desktop/self_developed/code/focus_app_national status --short
git -c safe.directory=C:/Users/Aze/Desktop/self_developed/code/focus_app_national diff --check

# 部署脚本参数检查
python deploy_to_board.py --help

# 板端正式服务和展示状态
ssh root@192.168.10.174 "systemctl is-active focus_app"
Invoke-RestMethod http://192.168.10.174:18080/api/demo/state

# 风扇当前所有权与只读状态
ssh root@192.168.10.174 "cat /sys/class/remoteproc/remoteproc0/state /sys/class/remoteproc/remoteproc0/firmware"
ssh root@192.168.10.174 "cat /run/edgefocus/fan-status.json"
```

交叉编译机为 `hu@192.168.10.6`。板端 IP 会随现场网络变化；最近使用过
`192.168.10.173`、`192.168.10.174` 和现场 `192.168.10.125`，部署前按当前任务或用户最新说明确认。部署入口是
`deploy_to_board.py`；它执行 ARM 全目标编译、安装包校验、板端备份、健康检查和失败回滚。

## App 同步修改入口

主机改动影响 App 时，先更新或引用以下文档，不要只在聊天里描述：

- `docs/integration/app-band-protocol-v2.1.md`：BLE/GATT、会话、手环和 App 交互协议的主机侧事实。
- `docs/integration/app-host-alignment-v3.md`：App 与主机的架构对齐。
- `docs/integration/gimbal-phase-control-app-handoff-2026-07-23.md`：云台 PRECHECK 阶段控制、故障状态和 App 展示/命令入口。
- `C:/Users/Aze/Desktop/APP/新app迭代必看/`：交给 App agent 的入口目录。

当前 App 侧边界：

- App 可以在未连接主机时做本地模式，但会话记录最终以主机为权威存储，App 做缓存与后续同步。
- App 上报手机原始类别：`未使用/学习/娱乐/其他`；最终分心等级由主机融合生成。
- App 负责前台交互、弹窗/屏幕闪烁提醒、预检页、专注页和设置页。
- App 预检页可展示云台异常，并允许发送“停止云台/强制释放”；不要要求主机提供 `stop_enabled`、`gimbal_async_motion_v1`、`gimbal_manual_step_v1` 这类未批准能力字段。

## 文档维护规则

- 改变模块所有权、生命周期、数据契约、模式或部署链时，同步更新
  `docs/architecture/current-architecture.md`。
- 协议字段变更同步更新 `docs/integration/app-band-protocol-v2.1.md`。
- 历史 `docs/superpowers/specs/` 不回写成“当前事实”；需要时新增变更记录并从当前架构文档链接。
- 工作树可能包含用户尚未提交的展示和板端修改。保留无关改动，不做 reset/checkout，不擅自提交。
