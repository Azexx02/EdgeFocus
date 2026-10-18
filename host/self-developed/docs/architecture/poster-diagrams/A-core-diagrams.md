# A 套：4 张核心展示图

A 套优先回答“系统是什么、如何判断、为何异构、怎样触达”。每张图都能单独使用。

## A1 系统全景架构

回答：EdgeFocus 是一个怎样闭环的产品？

```mermaid
flowchart LR
    USER["使用者"]:::external
    CAM["摄像头"]:::device
    APP["手机 App<br/>会话、手机事实、提醒、记录"]:::external
    BAND["蓝牙手环<br/>三级振动"]:::external
    WEB["浏览器展示页<br/>只读推理链"]:::external
    POWER["STUSB4500<br/>外部供电事实"]:::device

    subgraph MP2["STM32MP257 边缘主机"]
        A35["A35 / Linux<br/>会话编排·Fusion·BLE·存储"]:::host
        NPU["NPU<br/>YOLO11n Pose"]:::ai
        M33["Cortex-M33<br/>100 ms 风扇安全闭环"]:::realtime
    end

    G431["STM32G431<br/>云台实时控制"]:::realtime
    GIMBAL["双轴云台"]:::device
    FAN["PWM 风扇"]:::device

    USER --> CAM
    USER <--> APP
    CAM --> NPU --> A35
    APP <--> A35
    POWER --> A35
    A35 --> WEB
    A35 --> BAND
    A35 <--> M33 --> FAN
    A35 <--> G431 --> GIMBAL

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef realtime fill:#FFF3E0,stroke:#F97316,color:#431407;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
```

- 视觉、融合、会话与提醒全部在端侧闭环。
- App 是主要交互面，手环是低打扰提醒端。
- M33 与 G431 分担实时、安全和电机控制。

## A2 Edge AI 多源时序融合

回答：系统如何稳定地产生分神等级？

```mermaid
flowchart LR
    CAM["CameraFrame"]:::device --> Q["质量门<br/>清晰度·遮挡·新鲜度"]:::host
    Q --> POSE["YOLO11n Pose<br/>17 keypoints"]:::ai
    POSE --> SUBJECT["主体验证与选择"]:::ai
    SUBJECT --> FEAT["58 维归一化特征"]:::ai
    FEAT --> MLP["Prone Temporal MLP<br/>2 s / 16 steps"]:::ai
    SUBJECT --> PRES["Presence<br/>人在 / 不在 / 未知"]:::ai

    APP["App PhoneFact<br/>未使用·学习·娱乐·其他"]:::external
    APP --> TTL["2 s 心跳 / 5 s TTL"]:::host

    PRES --> FUSION["FusionCore<br/>连续性·适用性·episode"]:::ai
    MLP --> FUSION
    TTL --> FUSION
    FUSION --> L0["Level 0<br/>正常/专注"]:::host
    FUSION --> L13["Level 1–3<br/>三级分神"]:::fault
    FUSION --> WHY["reasons + episode<br/>近 30 s 证据时间线"]:::external

    classDef external fill:#E8F3FF,stroke:#1677FF,color:#10243E;
    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

- 视觉只承担“人在、趴下”等可验证事实。
- 手机类别来自 App，不由摄像头猜测。
- 单帧结果经过时序、TTL 与 episode 后才形成等级。
- 学习类只弱化手机分支，不覆盖独立视觉证据。

## A3 异构计算与实时控制

回答：为什么系统同时使用 A35、NPU、M33 与 G431？

```mermaid
flowchart TB
    subgraph NONRT["业务与系统域"]
        A35["A35 / Linux<br/>会话·融合·BLE·存储·安全编排"]:::host
    end

    subgraph AI_DOMAIN["AI 加速域"]
        NPU["NPU<br/>YOLO11n Pose"]:::ai
    end

    subgraph SAFETY["异构安全岛"]
        M33["Cortex-M33<br/>100 ms 风扇闭环"]:::realtime
        G431["STM32G431<br/>对齐·FOC·编码器·PWM"]:::realtime
    end

    CAM["摄像头"]:::device --> NPU --> A35
    A35 <-->|"RPMsg：温度/状态 1 Hz"| M33
    M33 -->|"PB10 PWM"| FAN["风扇"]:::device
    FAN -->|"PB11 tach"| M33
    A35 <-->|"UART4 115200"| G431
    A35 -->|"PD7 Enable"| G431
    G431 -->|"PD6 Fault 低有效"| A35
    G431 --> GIMBAL["云台电机"]:::device

    M33 -."A35 失联 3 s → 安全高转".-> SAFE["散热安全"]:::fault
    G431 -."Fault → 本地停驱动".-> STOP["云台安全停机"]:::fault

    classDef host fill:#E6FFFB,stroke:#08979C,color:#103B3D;
    classDef ai fill:#F3E8FF,stroke:#7C3AED,color:#2E1065;
    classDef realtime fill:#FFF3E0,stroke:#F97316,color:#431407;
    classDef device fill:#ECFDF3,stroke:#16A34A,color:#052E16;
    classDef fault fill:#FFF1F0,stroke:#DC2626,color:#450A0A;
```

- A35 做策略和资源编排，实时核不承载产品业务。
- NPU 专注姿态推理，释放 CPU 资源。
- M33 和 G431 即使业务侧异常也保持安全动作。

## A4 跨设备可靠协同

回答：提醒和状态如何可靠地到达 App 与手环？

```mermaid
sequenceDiagram
    autonumber
    participant Host as 板端 A35（会话权威）
    participant App as 手机 App（GATT Client）
    participant Band as 手环（GATT Server）

    App->>Host: BLE 连接 + hello / get_state
    Host-->>App: 当前 phase / snapshot / capabilities
    App->>Host: start_precheck / confirm / pause / end
    App->>Host: PhoneFact（2 s heartbeat）
    Host-->>App: push_alert(event_id, level, reasons)
    App->>Host: ACK_RECEIVED
    App->>App: 弹窗 / 屏幕闪烁 / 控制面板呈现
    App->>Host: ACK_PRESENTED
    Host->>Band: 振动命令（10 s TTL，高等级覆盖）
    Band-->>Host: 执行结果 / 状态
    App->>Host: ACK_HANDLED
    Note over Host,App: 断连期间事件进入 outbox；重连后幂等补发
```

- 板端对 App 是 GATT Server；App 是 Client。
- 板端对手环是 GATT Client；手环是 Server。
- 三阶段 ACK 区分收到、呈现与处理。
- TTL、outbox、幂等和等级覆盖限制过期提醒。
