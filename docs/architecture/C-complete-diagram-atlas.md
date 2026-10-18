# C 套：完整架构图谱

C0 是唯一系统母图，不计入十二张专题图。C01–C12 各自回答一个问题，是 A/B 图和后续重绘稿的规范源。

## C0 完整系统母图

回答：EdgeFocus 从感知、决策到提醒和硬件控制如何形成完整闭环？

```mermaid
flowchart LR
    USER["使用者"]:::external
    CAM["摄像头"]:::device
    APP["手机 App<br/>唯一主要交互面"]:::external
    BAND["蓝牙手环<br/>低打扰提醒"]:::external
    WEB["评委展示页<br/>只读状态/证据"]:::external
    STUSB["STUSB4500<br/>外部供电事实"]:::device

    subgraph MP2["STM32MP257 边缘主机"]
        subgraph A35["A35 / Linux 业务域"]
            SESSION["SessionOrchestrator<br/>PRECHECK / 正式会话"]:::host
            VCORE["视觉数据面<br/>质量·主体·特征"]:::ai
            FUSION["FusionCore<br/>Level 0 + Level 1–3"]:::ai
            BLE["App/Band BLE<br/>ACK·TTL·Outbox"]:::host
            STORE["SQLite / Evidence<br/>会话与报告"]:::host
            POLICY["Home/Go<br/>Reminder / Safety"]:::host
            DEMO["DemoRuntime<br/>预录 NPU / pre-log"]:::demo
        end
        NPU["NPU<br/>YOLO11n Pose"]:::ai
        M33["Cortex-M33<br/>风扇安全岛"]:::realtime
    end

    G431["STM32G431<br/>云台实时控制"]:::realtime
    GIMBAL["双轴云台"]:::device
    FAN["风扇"]:::device

    USER --> CAM
    USER <--> APP
    CAM --> NPU --> VCORE --> FUSION
    APP -->|"PhoneFact"| FUSION
    APP <--> BLE
    FUSION --> SESSION
    SESSION --> STORE
    SESSION --> BLE --> BAND
    SESSION --> WEB
    STUSB --> POLICY --> SESSION
    SESSION <--> M33 --> FAN
    SESSION <--> G431 --> GIMBAL
    SESSION -."正式会话互斥".-> DEMO
    DEMO --> WEB

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef realtime fill:#FFF3E0,stroke:#F97316,color:#431407;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef demo fill:#F3F4F6,stroke:#6B7280,color:#1F2937,stroke-dasharray:5 5;
```

关键边界：摄像头与供电是输入；云台和风扇是受控执行器；App 双向交互；手环和展示页接收主机状态；Demo 不进入正式记录或提醒。

## C01 产品交互与信息流

回答：用户如何通过 App、主机、手环和展示页使用产品？

```mermaid
flowchart LR
    USER["使用者"]:::external --> APP["App 专注页 / 横屏控制面板"]:::external
    APP --> PRE["PRECHECK<br/>状态显示·云台微调"]:::host
    PRE --> START["确认开始正式会话"]:::host
    START --> SESSION["主机权威会话<br/>FOCUSING / PAUSED / ENDING"]:::host
    APP -->|"学习/娱乐/其他/未使用"| SESSION
    SESSION --> LEVEL["Level 0 正常<br/>Level 1–3 三级分神"]:::ai
    LEVEL --> APP
    LEVEL --> BAND["手环三级振动"]:::external
    LEVEL --> WEB["现场只读展示<br/>证据叠加 + 近 30 s 时间线"]:::external
    SESSION --> REPORT["主机权威存储"]:::host
    REPORT --> APP
    OFFLINE["未连接主机时<br/>App 本地模式"]:::external --> CACHE["本地缓存会话"]:::external
    CACHE -->|"重连后幂等上传"| REPORT

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
```

浏览器展示页不控制正式会话；App 生成本地模式结果，但连接主机后以主机作为权威存储，避免记录分裂。

## C02 软件运行域与类型化数据面

回答：主程序如何分域，并通过哪些稳定数据类型协作？

```mermaid
flowchart TB
    subgraph PROCESS["进程常驻域"]
        CTX["AppContext"]:::host
        CMD["AppCommandService"]:::host
        BLE["BleGattServer / Band Client"]:::host
        DB["SQLiteStore"]:::host
        DEVICE["DeviceManager<br/>Power/Gimbal/Fan Bridge"]:::host
        HTTP["DemoHttpServer"]:::host
    end

    subgraph FORMAL["SessionRuntime"]
        ORCH["SessionOrchestrator"]:::host
        FEATURE["ISessionFeature"]:::host
        PIPE["DistractionPipelineFeature"]:::ai
        GFEATURE["GimbalSessionFeature"]:::host
        REMIND["ReminderPolicy"]:::host
    end

    subgraph DATA["类型化数据面"]
        FRAME["CameraFrame"]:::device
        PHONE["PhoneFact"]:::external
        ASS["DistractionAssessment"]:::ai
        SNAP["SessionSnapshot"]:::host
    end

    subgraph DEMO["DemoRuntime"]
        BOARD["BoardDemoRuntime"]:::demo
        DQUEUE["独立 frame/fact/snapshot"]:::demo
    end

    CTX --> ORCH
    CMD --> ORCH
    BLE <--> CMD
    DEVICE --> ORCH
    ORCH --> FEATURE
    FEATURE --> PIPE
    FEATURE --> GFEATURE
    FRAME --> PIPE
    PHONE --> PIPE
    PIPE --> ASS --> SNAP
    SNAP --> REMIND
    SNAP --> BLE
    SNAP --> DB
    HTTP --> BOARD --> DQUEUE
    ORCH -."Camera/NPU 租约互斥".-> BOARD

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef demo fill:#F3F4F6,stroke:#6B7280,color:#1F2937,stroke-dasharray:5 5;
```

`CameraFrame → DistractionAssessment → SessionSnapshot` 是核心事实链；旧 JSON/兼容状态只允许作为边缘投影。

## C03 正式会话生命周期

回答：每个会话阶段允许哪些功能运行？

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> PRECHECK: start_precheck
    PRECHECK --> FOCUSING: confirm_start
    PRECHECK --> IDLE: cancel / timeout
    FOCUSING --> PAUSED: pause
    PAUSED --> FOCUSING: resume
    FOCUSING --> ENDING: end
    PAUSED --> ENDING: end
    ENDING --> IDLE: commit + final event

    state PRECHECK {
        [*] --> VisionReady
        VisionReady --> AutoGimbalSearch
        AutoGimbalSearch --> UserFineTune
        UserFineTune --> ReadyToConfirm
    }
```

| Phase | Camera/NPU | Fusion/PhoneFact | 正式计时 | SQLite 会话记录 | 提醒 | 云台 |
|---|---:|---:|---:|---:|---:|---|
| IDLE | 按需关闭 | 否 | 否 | 否 | 否 | 保持/安全停机 |
| PRECHECK | 是 | 否 | 否 | 否 | 否 | 自动搜索 + 人工微调 |
| FOCUSING | 是 | 是 | 是 | 是 | 是 | 会话内跟踪 |
| PAUSED | 可低频 | 停止累计 | 暂停 | 保留 | 否 | 保持安全姿态 |
| ENDING | 停止并释放 | 收尾 | 停止 | 提交报告 | final event | 正常停止 |

PRECHECK 可以过视觉模型，但其结果不会进入正式 Fusion、episode、计时或记录。

## C04 Edge AI 感知、时序与 Fusion

回答：分神等级如何从可解释证据中产生？

```mermaid
flowchart LR
    FRAME["CameraFrame"]:::device --> QUAL["质量与新鲜度门"]:::host
    QUAL --> POSE["YOLO11n Pose<br/>17 keypoints"]:::ai
    POSE --> SUBJECT["主体选择<br/>连续 ID / 几何约束"]:::ai
    SUBJECT --> PRES["Presence<br/>PRESENT / NOT_PRESENT / UNKNOWN"]:::ai
    SUBJECT --> FEAT["58-d normalized features"]:::ai
    FEAT --> WINDOW["2 s / 16 steps"]:::ai
    WINDOW --> PRONE["Temporal Prone MLP"]:::ai

    RAW["App PhoneFact<br/>OFF / LEARNING / ENTERTAINMENT / OTHER"]:::external --> FRESH["2 s heartbeat<br/>5 s freshness TTL"]:::host
    PRES --> FUSION["FusionCore<br/>适用性·连续性·原因"]:::ai
    PRONE --> FUSION
    FRESH --> FUSION
    FUSION --> EP["Episode accumulator<br/>持续时间与等级跃迁"]:::ai
    EP --> OUT["DistractionAssessment<br/>applicability + level + reasons"]:::host
    OUT --> L0["Level 0 正常"]:::host
    OUT --> L13["Level 1–3 三级分神"]:::fault
    OUT --> EVIDENCE["证据叠加 / 近 30 s 四轨时间线"]:::external

    LEARN["学习类只弱化 Phone 分支"]:::host -.-> FUSION
    ABSENT["NOT_PRESENT / UNKNOWN<br/>不产生分神提醒"]:::host -.-> OUT

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

娱乐与其他类别互切不清空已累积的无关使用；切换到学习或未使用后才按策略清零或逐步下降。

## C05 App/手环跨设备可靠链

回答：连接、状态、提醒和断线恢复如何保持一致？

```mermaid
sequenceDiagram
    autonumber
    participant App as App / GATT Client
    participant Host as 板端 / GATT Server + 会话权威
    participant Store as SQLite + Event Outbox
    participant Band as 手环 / GATT Server

    App->>Host: connect + hello(protocol, capabilities, last_event_id)
    Host-->>App: hello_ack + authoritative state
    App->>Host: get_state / session command(command_id)
    Host->>Store: 幂等状态事务
    Store-->>App: snapshot / report
    loop 每 2 秒，正式会话内
        App->>Host: PhoneFact(category, observed_at)
    end
    Note over Host: 超过 5 秒 → phone stale
    Host->>Store: enqueue push_alert(event_id, level, reasons)
    Store-->>App: notification
    App->>Host: ACK_RECEIVED
    App->>App: 弹窗 / 屏幕闪烁 / 前台控制面板
    App->>Host: ACK_PRESENTED / ACK_HANDLED
    Host->>Band: vibrate(level, expires_at)
    Band-->>Host: status / result
    Note over Host,Band: 10 秒 TTL；重连重试；高等级覆盖低等级
    alt App 断连
        Store->>Store: 保存未完成关键事件
        App->>Host: reconnect + hello(last_event_id)
        Host-->>App: state resync + missing events
    end
```

App 断连不停止主机会话；App 的 UI 状态必须从主机快照恢复，不能以本地按钮状态反推主机 phase。

## C06 A35/NPU/M33/G431 异构职责

回答：四个计算域如何分工并隔离故障？

```mermaid
flowchart TB
    subgraph A35LANE["A35 / Linux｜非实时产品域"]
        A35["Session · Fusion · BLE · SQLite<br/>Power · Resource Lease · Safety Orchestration"]:::host
    end
    subgraph NPULANE["NPU｜AI 加速域"]
        NPU["YOLO11n Pose"]:::ai
    end
    subgraph M33LANE["Cortex-M33｜散热安全岛"]
        M33["100 ms control<br/>PB10 PWM / PB11 tach"]:::realtime
    end
    subgraph G431LANE["STM32G431｜运动实时域"]
        G431["Boot Align · HOLD · FOC<br/>Encoder · Motor PWM · Local Fault Stop"]:::realtime
    end

    CAM["Camera"]:::device --> NPU --> A35
    A35 <-->|"RPMsg：1 Hz 温度/状态"| M33
    M33 --> FAN["Fan"]:::device
    A35 <-->|"UART4 115200"| G431
    A35 -->|"PD7 Enable"| G431
    G431 -->|"PD6 Fault low-active"| A35
    G431 --> GIMBAL["Gimbal"]:::device
    M33 -."3 s TTL → safe high speed".-> SAFE1["局部降级"]:::fault
    G431 -."Fault → local stop".-> SAFE2["固定相机继续"]:::fault

    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef realtime fill:#FFF3E0,stroke:#F97316,color:#431407;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

M33 不控制云台或供电，G431 不承担会话逻辑，A35 不直接生成电机相电流/PWM。

## C07 云台视觉控制与安全状态机

回答：云台如何完成预检、跟踪、人工控制和故障恢复？

```mermaid
stateDiagram-v2
    [*] --> DISABLED
    DISABLED --> ALIGNING: PING OK + PD7 Enable 上升沿
    ALIGNING --> READY: BOOT_ALIGN,OK
    ALIGNING --> FAULT: timeout / PD6 low
    READY --> PRECHECK_SCAN: PRECHECK auto search
    PRECHECK_SCAN --> READY: target framed / scan exhausted
    READY --> MANUAL: App manual lease
    MANUAL --> READY: release / timeout
    READY --> TRACKING: FOCUSING + valid target
    TRACKING --> READY: pause / target stale
    READY --> STOPPING: normal session end
    TRACKING --> STOPPING: normal session end
    STOPPING --> DISABLED: STOP → OK,STOP → Enable=0
    PRECHECK_SCAN --> FAULT: PD6 low / UART failure
    MANUAL --> FAULT: PD6 low / UART failure
    TRACKING --> FAULT: PD6 low / UART failure
    FAULT --> DISABLED: G431 local stop + A35 revoke Enable
    DISABLED --> ALIGNING: low-rate health recovery
```

```mermaid
flowchart LR
    VISION["人物位置/构图误差"]:::ai --> CTRL["GimbalSessionFeature<br/>限位·死区·速率限制"]:::host
    APP["App 人工微调"]:::external --> LEASE["Manual Control Lease"]:::host --> CTRL
    CTRL <-->|"UART4 命令/状态"| G431["G431 实时执行"]:::realtime
    CTRL -->|"PD7 Enable"| G431
    G431 -->|"PD6 Fault"| CTRL
    G431 --> MOTOR["云台电机"]:::device

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef realtime fill:#FFF3E0,stroke:#F97316,color:#431407;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
```

PD4 仅为 BOOT0 控制，不画成复位。故障路径不依赖先收到 STOP ACK；G431 本地先停驱动，A35 撤销 Enable 并停止目标下发。

## C08 M33 风扇闭环与启动回退

回答：散热如何跨核闭环，并在任一侧异常时保持安全？

```mermaid
flowchart LR
    THERMAL["cpu-thermal"]:::device --> BRIDGE["A35 bridge<br/>1 Hz 温度/策略"]:::host
    BRIDGE <-->|"RPMsg handshake / telemetry"| M33["M33 100 ms loop"]:::realtime
    M33 --> POLICY["常温低噪曲线"]:::realtime
    POLICY --> HOT["超过阈值：逐步升速<br/>温度开始下降后保持"]:::realtime
    HOT --> COOL["低于阈值 5–10°C<br/>恢复正常曲线"]:::realtime
    COOL -->|"PB10 PWM"| FAN["Fan"]:::device
    FAN -->|"PB11 tach"| M33
    M33 -."3 s RPMsg TTL".-> SAFE["安全高转速"]:::fault
    M33 -."tach 堵转/异常".-> SAFE

    BOOT["systemd boot"]:::host --> LOADER["remoteproc loader"]:::host --> HEALTH{"M33 health"}:::host
    HEALTH -->|"healthy"| OWN_M33["M33 owns PB10/PB11<br/>Linux PWM/timer unbound"]:::realtime
    HEALTH -->|"failed"| OWN_LINUX["Linux fallback owns pins<br/>remoteproc offline"]:::fault
    OWN_LINUX -->|"恢复后原子移交"| OWN_M33

    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef realtime fill:#FFF3E0,stroke:#F97316,color:#431407;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

禁止 M33 与 Linux fallback 同时写 PWM/tach；所有权切换必须先释放旧控制者再启用新控制者。

## C09 外部供电与 Home/Go 性能策略

回答：外部供电如何改变业务性能，同时不降低安全能力？

```mermaid
flowchart LR
    STUSB["STUSB4500QTR<br/>I2C4 PD10 SDA / PD11 SCL"]:::device --> RAW["原始供电读数"]:::host
    RAW --> FILTER["3 次一致采样<br/>250 ms 间隔"]:::host
    FILTER --> STATE{"PRESENT / ABSENT / UNKNOWN"}:::host
    STATE -->|"PRESENT"| AUTO_HOME["Auto Home"]:::host
    STATE -->|"ABSENT"| AUTO_GO["Auto Go"]:::host
    STATE -->|"UNKNOWN + no override"| AUTO_GO
    APP["App 手动档位"]:::external --> OVERRIDE["Manual override<br/>保持至确认插拔 epoch"]:::host
    FILTER -->|"confirmed transition"| EPOCH["新 plug epoch<br/>清除 override"]:::host
    AUTO_HOME --> PROFILE["WorkloadProfilePolicy"]:::host
    AUTO_GO --> PROFILE
    OVERRIDE --> PROFILE
    EPOCH --> STATE
    PROFILE --> HOME["Home：视觉约 4 FPS"]:::ai
    PROFILE --> GO["Go：视觉约 3 FPS"]:::ai
    PROFILE --> FIXED["不降频：BLE、Fault、M33、G431 实时环"]:::fault

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

Home/Go 是工作负载档位，不是“插电/电池模式”的电量展示；硬件不具备电量读取时，产品不伪造百分比。

## C10 正式会话、预录 NPU 与 pre-log 隔离

回答：现场真实展示、预录推理和纯前端保底如何互不污染？

```mermaid
flowchart TB
    UI["展示页模式开关"]:::external --> LEASE{"FormalResourceLease"}:::host

    subgraph LIVE["LIVE_CAMERA｜正式产品链"]
        LIVEIN["现场摄像头"]:::device --> LIVEMODEL["NPU + Fusion"]:::ai --> LIVESNAP["正式 SessionSnapshot"]:::host
        LIVESNAP --> FORMALOUT["提醒 / SQLite / App / Band"]:::host
    end

    subgraph REC["RECORDED_NPU｜预录实时推理"]
        SELECT["人工选择分段视频"]:::demo --> PLAYER["按进入时启动的播放器"]:::demo --> RNPU["NPU 实时推理"]:::ai --> RENDER["演示证据链"]:::demo
        RENDER --> MANUALPHONE["页面可手动切换 PhoneFact"]:::demo
    end

    subgraph LOG["FRONTEND_REPLAY｜NPU 保底"]
        LOGVIDEO["预录视频"]:::demo --> SYNC["正常 NPU 运行时保存的同步 pre-log"]:::demo --> ANIM["纯前端关键点/时间线动画"]:::demo
        ANIM --> FIXEDPHONE["同步手机轨<br/>不可手动改"]:::demo
    end

    LEASE -->|"会话启动"| LIVE
    LEASE -->|"IDLE + 手动开启"| REC
    LEASE -->|"手动开启 / NPU fail"| LOG
    REC -."退出即停止播放器和 NPU".-> STOP["释放资源/清空播放器状态"]:::fault
    LOG -."退出即停止".-> STOP
    REC -."禁止写正式存储和提醒".-> GUARD["Demo guard"]:::fault
    LOG -."禁止写正式存储和提醒".-> GUARD

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef demo fill:#F3F4F6,stroke:#6B7280,color:#1F2937,stroke-dasharray:5 5;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

真实摄像头模式仍按正常会话启动；预录模式只是展示视觉推理链，不冒充正式会话。

## C11 故障降级矩阵

回答：每类故障由谁检测、如何降级、保留什么、怎样恢复？

| 故障域 | 检测信号 | 立即动作 | 保留能力 | 用户可见状态 | 自动恢复 / 责任域 |
|---|---|---|---|---|---|
| Camera / NPU | 打开失败、帧超时、推理 stale | 视觉置 UNKNOWN，停止使用旧证据 | 会话、手机事实、BLE | 视觉不可用/降级 | worker 限频重启 / A35 |
| PhoneFact | 超过 5 s TTL | 手机分支 stale | 独立视觉判断 | 手机状态未同步 | 心跳恢复即生效 / A35+App |
| App BLE | 断连、ACK 超时 | 关键事件进入 outbox | 推理、存储、手环 | App 重连中 | hello/get-state/幂等补发 / A35+App |
| 手环 BLE | 连接失败、10 s TTL | 重试或丢弃过期低优先级提醒 | App 提醒、正式会话 | 手环未连接 | 周期搜索、等级覆盖 / A35 |
| SQLite | 事务/磁盘错误 | 保留内存快照，不伪报已落盘 | 实时判断与提醒 | 记录保存异常 | 限次重试并上报 / Storage |
| G431 / Fault | PD6 低、UART/对齐失败 | G431 本地停驱动；A35 撤销 PD7并停止目标 | 固定相机视觉、会话 | 云台已降级 | 低频检查后重新对齐 / A35+G431 |
| M33 / RPMsg | 3 s TTL、health 失败 | 安全高转或互斥切 Linux fallback | 会话与 AI | 散热安全模式 | health 恢复后原子移交 / M33+A35 |
| STUSB4500 | I2C 失败、样本不一致 | UNKNOWN；无覆盖时 Go | 全部安全链 | 供电未知/Go | 周期采样，确认插拔清覆盖 / Power |
| Demo 资源冲突 | Camera/NPU lease 失败 | Demo 不启动或转 pre-log | 正式会话 | 演示资源占用 | 正式资源释放后再试 / DemoRuntime |
| 新 release | health check 失败 | 不切换或原子回滚 | 上一 release | 更新失败/已回滚 | 保留诊断 / Deploy |

安全优先级：散热安全与云台停机优先于体验；正式会话数据一致性优先于演示；过期证据一律失效而不是沿用。

## C12 构建、部署与回滚

回答：代码、模型与演示素材怎样安全到达板端？

```mermaid
flowchart LR
    subgraph SOFTWARE["软件/模型发布"]
        WS["Windows 工作区"]:::external --> CHECK["tests + config validation"]:::host
        CHECK --> CROSS["ARM 交叉编译机"]:::host
        CROSS --> RELEASE["不可变 release<br/>binary + model + web assets"]:::host
        RELEASE --> STAGE["板端 staging"]:::device
        STAGE --> VERIFY["hash / dependency / API health"]:::host
        VERIFY -->|"pass"| ATOMIC["原子切换 current"]:::host
        VERIFY -->|"fail"| KEEP["保留上一 release"]:::fault
        ATOMIC --> RESTART["systemd restart"]:::host
        RESTART --> SMOKE{"camera/NPU/BLE/UI smoke"}:::host
        SMOKE -->|"fail"| ROLLBACK["atomic rollback"]:::fault
    end

    subgraph MEDIA["演示素材独立同步"]
        ASSET["Windows MP4 / pre-log"]:::external --> SHA["SHA-256 增量清单"]:::host
        SHA --> MEDIA_DIR["板端 assets/demo"]:::device
    end

    MEDIA_DIR --> SMOKE

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

媒体文件不进入源码提交或 ARM release；软件回滚不会丢失独立素材，素材同步失败也不会替换正式程序。
