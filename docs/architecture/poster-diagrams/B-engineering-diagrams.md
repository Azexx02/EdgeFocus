# B 套：9 张工程架构图

B 套用于说明进程边界、资源所有权、状态时序、安全回退与发布链。

## B1 主程序三个运行域

```mermaid
flowchart TB
    subgraph ALWAYS["进程级常驻域"]
        CTX["AppContext"]:::host
        BLE["BLE / AppCommandService"]:::host
        STORE["SQLiteStore"]:::host
        POWER["PowerSourceService"]:::host
        GHW["GimbalHardwareService"]:::host
        HTTP["HTTP / 展示发布"]:::host
    end

    subgraph FORMAL["正式 SessionRuntime"]
        ORCH["SessionOrchestrator"]:::host
        VISION["DistractionPipelineFeature"]:::ai
        PHONE["PhoneFactBuffer"]:::host
        POLICY["ReminderPolicy"]:::host
        SNAP["SessionSnapshotPublisher"]:::host
    end

    subgraph DEMO["无会话 DemoRuntime"]
        REC["RECORDED_NPU"]:::demo
        REPLAY["FRONTEND_REPLAY / pre-log"]:::demo
    end

    CTX --> ORCH
    BLE <--> ORCH
    ORCH --> STORE
    POWER --> ORCH
    GHW --> ORCH
    ORCH --> VISION --> SNAP --> BLE
    PHONE --> VISION --> POLICY
    HTTP --> REC
    HTTP --> REPLAY
    ORCH -."正式会话占用 Camera/NPU 时禁止".-> DEMO

    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef demo fill:#F3F4F6,stroke:#6B7280,color:#1F2937,stroke-dasharray:5 5;
```

常驻服务负责接入与设备；SessionRuntime 只在正式会话存在；DemoRuntime 只在无正式会话时由页面启动。

## B2 正式会话状态机

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> PRECHECK: start_precheck
    PRECHECK --> FOCUSING: confirm_start
    PRECHECK --> IDLE: cancel / timeout
    FOCUSING --> PAUSED: pause
    PAUSED --> FOCUSING: resume
    FOCUSING --> ENDING: end / fatal session error
    PAUSED --> ENDING: end
    ENDING --> IDLE: report committed / final event queued

    note right of PRECHECK
      视觉与云台准备可运行
      不计时、不入库
      不进入 Fusion/episode/提醒
    end note
    note right of FOCUSING
      正式计时
      PhoneFact + Fusion + 存储 + 提醒
    end note
    note right of PAUSED
      保留上下文
      暂停累计与提醒
    end note
```

App 页面把主机 `FOCUSING` 映射为 `RUNNING`；最终报告展示态属于 App，不是新的主机正式 phase。

## B3 软件模块与类型化数据面

```mermaid
flowchart LR
    subgraph INPUT["事实输入"]
        FRAME["CameraFrame"]:::device
        PHONE["PhoneFact"]:::external
        PWR["PowerSourceState"]:::device
    end

    subgraph SESSION["会话域"]
        ORCH["SessionOrchestrator"]:::host
        RT["SessionRuntime"]:::host
        FEATURE["ISessionFeature"]:::host
        PIPE["DistractionPipelineFeature"]:::ai
        GIM["GimbalSessionFeature"]:::host
    end

    subgraph CORE["核心数据面"]
        ASSESS["DistractionAssessment"]:::ai
        SNAP["SessionSnapshot"]:::host
        POLICY["ReminderPolicy"]:::host
    end

    subgraph PORTS["平台端口"]
        BLE["BleGattServer / AppEventOutbox"]:::host
        DB["SQLiteStore"]:::host
        DEMO["BoardDemoRuntime"]:::demo
        DEV["DeviceManager / Hardware Services"]:::host
    end

    FRAME --> PIPE
    PHONE --> PIPE
    PWR --> ORCH
    ORCH --> RT --> FEATURE
    FEATURE --> PIPE --> ASSESS --> SNAP --> POLICY
    SNAP --> BLE
    SNAP --> DB
    GIM --> DEV
    ORCH -."资源租约".-> DEMO

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef demo fill:#F3F4F6,stroke:#6B7280,color:#1F2937,stroke-dasharray:5 5;
```

跨模块只传类型化事实、判定和快照；兼容投影不得成为新的核心依赖。

## B4 可靠事件与断线恢复

```mermaid
sequenceDiagram
    participant App as App
    participant Gatt as BleGattServer
    participant Cmd as AppCommandService
    participant Orch as SessionOrchestrator
    participant Box as AppEventOutbox
    participant DB as SQLiteStore

    App->>Gatt: connect + hello(last_event_id)
    Gatt->>Cmd: negotiated capabilities
    Cmd->>Orch: get_state
    Orch-->>App: authoritative phase + snapshot
    App->>Cmd: session command(command_id)
    Cmd->>Orch: idempotent transition
    Orch->>DB: transaction / report state
    Orch->>Box: enqueue critical event(event_id)
    Box-->>App: notify push_alert / push_session_end
    App->>Box: ACK_RECEIVED → ACK_PRESENTED → ACK_HANDLED
    alt BLE disconnects
        Box->>Box: retain unhandled events
        App->>Gatt: reconnect + hello(last_event_id)
        Box-->>App: replay missing events only
    end
```

命令用 `command_id` 幂等；事件用 `event_id` 去重；主机状态始终覆盖 App 猜测状态。

## B5 云台视觉控制闭环

```mermaid
flowchart LR
    CAM["唯一 CameraSource"]:::device --> OBS["人物目标与构图误差"]:::ai
    OBS --> MODE{"当前阶段"}:::host
    MODE -->|"PRECHECK"| SEARCH["上下左右受限搜索<br/>找到合适目标即停止"]:::host
    MODE -->|"FOCUSING"| TRACK["会话内人脸/人物跟踪"]:::host
    MODE -->|"人工微调租约"| MANUAL["App 云台控制"]:::external
    SEARCH --> GHW["GimbalHardwareService"]:::host
    TRACK --> GHW
    MANUAL --> GHW
    GHW <-->|"UART4 115200<br/>PING/STAT/ANGLE/SET/TRACK"| G431["STM32G431<br/>对齐·FOC·编码器·PWM"]:::realtime
    GHW -->|"PD7 Enable"| G431
    G431 -->|"PD6 Fault 低有效"| GHW
    G431 --> MOTOR["双轴云台"]:::device
    G431 -."Fault：本地先停驱动".-> SAFE["安全停机"]:::fault
    GHW -."撤销 Enable / 停止目标<br/>低频健康检查".-> SAFE
    SAFE -."恢复并重新对齐".-> G431

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef realtime fill:#FFF3E0,stroke:#F97316,color:#431407;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

PD4 是 BOOT0，不是复位；云台故障只关闭跟踪，固定相机视觉与正式会话继续。

## B6 M33 风扇安全闭环

```mermaid
flowchart LR
    TEMP["Linux cpu-thermal"]:::device --> BRIDGE["A35 fan bridge<br/>1 Hz"]:::host
    BRIDGE <-->|"RPMsg 握手/温度/状态"| M33["Cortex-M33<br/>100 ms 控制循环"]:::realtime
    M33 -->|"PB10 PWM"| FAN["风扇<br/>约 6000 RPM 安全档"]:::device
    FAN -->|"PB11 tach"| M33
    M33 --> POLICY["正常曲线 + 高温追升<br/>降温后保持 / 低 5–10°C 恢复"]:::realtime
    BRIDGE -."3 s 未更新".-> TTL["安全高转速"]:::fault
    M33 -."堵转/转速异常".-> TTL

    subgraph OWN["互斥控制权"]
        M33MODE["M33 在线：Linux PWM/timer unbound"]:::realtime
        FALLBACK["Linux fallback：remoteproc offline"]:::fault
    end
    M33MODE <-->|"healthcheck 原子移交"| FALLBACK

    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef realtime fill:#FFF3E0,stroke:#F97316,color:#431407;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

任何时刻只有一个控制者写 PB10/PB11；A35 失联不使风扇失控。

## B7 外部供电与 Home/Go 策略

```mermaid
flowchart LR
    PD["STUSB4500QTR<br/>I2C4 PD10/PD11"]:::device --> SAMPLE["3 个一致样本<br/>每次间隔 250 ms"]:::host
    SAMPLE --> STATE{"ExternalPowerState"}:::host
    STATE -->|"PRESENT"| HOME["Home<br/>视觉约 4 FPS"]:::host
    STATE -->|"ABSENT"| GO["Go<br/>视觉约 3 FPS"]:::host
    STATE -->|"UNKNOWN 且无手动覆盖"| GO
    MANUAL["App 手动 Home / Go"]:::external --> OVERRIDE["Manual Override"]:::host
    OVERRIDE --> HOME
    OVERRIDE --> GO
    SAMPLE -->|"确认的插拔 epoch"| CLEAR["清除手动覆盖"]:::host
    CLEAR --> STATE
    HOME --> CADENCE["VisionCadencePolicy"]:::ai
    GO --> CADENCE
    HOME --> SAFE["BLE / Fault / M33 / G431<br/>安全环不降频"]:::fault
    GO --> SAFE

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

系统只读取是否有外部供电，不显示不存在的电池电量；档位只改变业务节拍，不削弱安全链路。

## B8 正式会话与演示隔离

```mermaid
flowchart TB
    LEASE{"Camera / NPU ResourceLease"}:::host

    subgraph LIVE["LIVE_CAMERA 正式会话"]
        LCAM["现场摄像头"]:::device --> LNPU["NPU"]:::ai --> LFUSION["Fusion + Reminder"]:::ai
        LFUSION --> LDB["SQLite / App / Band"]:::host
    end

    subgraph RNPU["RECORDED_NPU 演示"]
        VIDEO["分段预录视频<br/>进入模式才播放"]:::demo --> DNPU["NPU 实时推理"]:::ai --> DSNAP["演示 snapshot"]:::demo
    end

    subgraph REPLAY["FRONTEND_REPLAY 保底"]
        PVIDEO["预录视频"]:::demo --> PRELOG["已保存视觉 pre-log<br/>含同步手机轨"]:::demo --> ANIM["纯前端证据动画"]:::demo
    end

    LEASE -->|"正式会话优先"| LIVE
    LEASE -->|"仅 IDLE + 人工打开"| RNPU
    LEASE -->|"NPU 失败自动回退 / 手动开关"| REPLAY
    RNPU -."不得写正式记录/提醒".-> GUARD["隔离守卫"]:::fault
    REPLAY -."不得写正式记录/提醒".-> GUARD

    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef demo fill:#F3F4F6,stroke:#6B7280,color:#1F2937,stroke-dasharray:5 5;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

离开演示模式立即停止视频和推理；再次进入可任意选择片段，不继承上次播放器状态。

## B9 构建、发布与回滚

```mermaid
flowchart LR
    subgraph CODE["软件发布泳道"]
        SRC["Windows 工作区<br/>源码/配置/模型"]:::external --> BUILD["A35 交叉编译机"]:::host
        BUILD --> REL["不可变 ARM release"]:::host
        REL --> STAGE["板端 staging"]:::device
        STAGE --> HEALTH{"health check"}:::host
        HEALTH -->|"pass"| SWITCH["原子切换 current"]:::host
        HEALTH -->|"fail"| ROLLBACK["保留/恢复上一 release"]:::fault
    end

    subgraph MEDIA["演示素材泳道"]
        MP4["Windows demo assets"]:::external --> HASH["SHA-256 增量校验"]:::host
        HASH --> MDIR["板端 media 目录"]:::device
    end

    SWITCH --> SERVICE["systemd restart + API/UI smoke test"]:::host
    MDIR --> SERVICE

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

MP4 不进入源码包或 ARM release；软件与素材独立同步、独立校验。
