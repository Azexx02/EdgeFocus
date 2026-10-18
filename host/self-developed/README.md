# 主机软件 · 自研底板版

运行在 **STM32MP257 自研载板** 上的 EdgeFocus 主机软件，是自研路线的最终版本，业务代码继承自正点原子版的最后一版，并扩展了自研板的供电、云台、M33 风扇与展示能力。

## 运行域模型

```text
AppContext（进程常驻）
├── BLE GATT Server（App） / BLE GATT Client（手环）
├── SQLite、资源租约、HTTP 展示页
├── PowerSourceService（STUSB4500） + WorkloadProfilePolicy（Home/Go）
├── GimbalHardwareService（条件创建，进程级硬件所有者）
├── M33 fan bridge（独立平台服务，不进入会话）
└── SessionOrchestrator
    └── SessionRuntime（每场正式会话新建）
        ├── DistractionPipelineFeature（视觉 + PhoneFact + Fusion）
        ├── GimbalSessionFeature（可选）
        ├── ReminderPolicy
        └── SessionSnapshotPublisher
```

边界要点：

- 正式会话只能由 `SessionOrchestrator` 创建，且只允许 `LIVE_CAMERA`。
- 正式会话与 `BoardDemoRuntime` 互斥；Demo 不写正式统计、不发真实提醒。
- `SystemStateStore` 只是兼容投影面，不新增判定语义。
- 视觉只输出 Presence / Prone / 人体框与关键点；手机类别只来自 App。
- `NOT_PRESENT` / `UNKNOWN` 不是分神等级；只有 `ACTIVE` 才允许 Level 0–3。
- 手环与 App 共用 BlueZ 适配器，当前硬件不假设二者可同时连接。
- 风扇是独立板级服务：Linux bridge 只上报 `cpu-thermal`，M33 独占 PWM / tach。

## 目录

| 目录 | 职责 |
| --- | --- |
| `session/` | 会话状态机、运行域、快照与兼容投影 |
| `core/distraction/` | 与 UI/BLE/会话无关的视觉、时序、Fusion 静态库 |
| `vision/` | STAI Pose、Prone MLP 与会话特性适配 |
| `camera/` | 唯一现场采集与共享预览帧 |
| `behavior/` | 手机事实缓冲与 assessment 适配 |
| `network/` | App GATT Server、命令门面、事件 outbox、Demo HTTP |
| `policy/` | 提醒等级、冷却与渠道路由 |
| `device/` | 手环 GATT Client、云台预检控制器、保留但禁用的屏幕/声音驱动 |
| `demo/` | 预录 NPU / 前端 pre-log 演示运行域 |
| `storage/` | SQLite 会话、事件、报告 |
| `platform/fan/` | 温控核心、RPMsg bridge、资源切换/健康检查/回退 |
| `platform/gimbal/` | UART/GPIO 硬件状态机、协议解析、bring-up 工具 |
| `platform/power/` | STUSB4500 只读检测、去抖与 Home/Go 策略 |
| `firmware/m33/fan_controller/` | M33 控制任务与 ATK HAL/OpenAMP 板级适配 |
| `assets/demo/` | 演示素材与同名 pre-log |
| `docs/` | 当前架构、硬件实测、协议对齐与历史设计 |

## 构建

```bash
cmake -B build-national-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-national-host -j
```

常用选项：

| 选项 | 说明 |
| --- | --- |
| `EDGEFOCUS_WITH_OPENCV` | 相机采集与预处理 |
| `EDGEFOCUS_WITH_STAI_MPU` | NPU 姿态模型适配 |
| `EDGEFOCUS_WITH_DEMO_HTTP` | 浏览器展示页 |
| `EDGEFOCUS_WITH_OPENH264` | 低延迟预览编码 |
| `EDGEFOCUS_WITH_FAN_BRIDGE` | A35 ↔ M33 风扇 bridge |
| `EDGEFOCUS_BUILD_TESTS` | CTest 测试 |

板端交叉编译与部署：

```powershell
python deploy_to_board.py --help
$env:EDGEFOCUS_BOARD='root@<board-ip>'
python deploy_to_board.py
```

`deploy_to_board.py` 使用 SSH BatchMode（密钥），不包含密码；默认编译机与板端地址可用参数覆盖。

## 自研板运行注意

- 相机、NPU、云台、手环均为可选能力，启动时按设备节点与健康检查决定是否启用。
- 屏幕与音频仍处于 bring-up 边界，不作为核心功能硬依赖。
- 云台在 `motion_verified=false` 或机械参数不完整时必须自动拒绝启用。
- 外部供电事实只来自 STUSB4500；Home/Go 只调整视觉采样节奏，不降低 BLE、Fault、风扇与 G431 控制频率。

## 文档

- [`docs/architecture/current-architecture.md`](docs/architecture/current-architecture.md) — 当前工作树实际架构（先读）
- [`docs/architecture/edgefocus-complete-architecture.md`](docs/architecture/edgefocus-complete-architecture.md) — 系统架构母文档
- [`docs/integration/app-band-protocol-v2.1.md`](docs/integration/app-band-protocol-v2.1.md) — App / 主机 / 手环协议
- [`docs/integration/app-host-alignment-v3.md`](docs/integration/app-host-alignment-v3.md) — App 与主机对齐
- [`docs/integration/gimbal-phase-control-app-handoff-2026-07-23.md`](docs/integration/gimbal-phase-control-app-handoff-2026-07-23.md) — 云台 PRECHECK 交接
- [`docs/hardware/`](docs/hardware/) — 板级实测与 bring-up 结果
- [`docs/superpowers/specs/`](docs/superpowers/specs/) — 历史设计依据（不等于当前实现）
