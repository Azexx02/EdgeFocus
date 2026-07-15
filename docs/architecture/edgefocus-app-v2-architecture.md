# EdgeFocus 新版 App 架构

> 版本：2026-07-16  
> 平台：Android 13 / Jetpack Compose / Hilt / Room / BLE GATT  
> 主机协议：EdgeFocus schema 2.1

## 1. 总体架构图

```mermaid
flowchart TB
    subgraph ENTRY["入口与系统容器"]
        NFC["NFC / edgefocus://controlpanel"]
        MAIN["MainActivity<br/>竖屏 App 容器"]
        PANEL["ControlPanelActivity<br/>独立横屏控制面板"]
        APP["EdgeFocusApp<br/>进程级组件启动"]
    end

    subgraph PRESENTATION["Presentation · Jetpack Compose"]
        NAV["NavigationGraph"]
        HOME["HomeScreen<br/>首页计时"]
        DASH["DashboardScreen<br/>历史与统计"]
        SETTINGS["SettingsScreen<br/>App / BLE / 屏保设置"]
        PAGER["ControlPanelScreen<br/>HorizontalPager"]
        PAGE1["Page 1<br/>计时 / PRECHECK"]
        PAGE2["Page 2<br/>本次会话统计"]
        PAGE3["Page 3<br/>控制面板设置"]
        OVERLAY["AlertOverlay<br/>全局主机提醒"]
        SAVER["BlackScreenSaver<br/>App 内黑屏层"]
        VM["Home / ControlPanel / Settings<br/>Dashboard / BLE ViewModels"]
    end

    subgraph APPLICATION["Application · 进程级协调"]
        SESSION["AppSessionCoordinator<br/>唯一会话生命周期"]
        TIMER["TimerStateHolder<br/>共享计时与主机状态运行时"]
        ALERT["AlertDeliveryCoordinator<br/>可靠事件交付"]
        PRECHECK["PrecheckController<br/>预检与云台控制"]
        CONNECTION["ConnectionSupervisor<br/>连接协商 / READY / 恢复"]
        MONITOR["AppMonitorService<br/>PhoneFact 前台服务"]
    end

    subgraph DOMAIN["Domain · 模型与策略"]
        SESSION_MODEL["Session owner / TimerState<br/>AppUiSnapshot"]
        EVENT_POLICY["ReliableEventPolicy<br/>TTL / ACK 阶段 / 去重"]
        PHONE_POLICY["PhoneFact Policies<br/>is_in_use / usage_state"]
        PRECHECK_POLICY["SequencePolicy<br/>PRECHECK 命令规则"]
    end

    subgraph DATA["Data · 协议、仓库与持久化"]
        CODEC["HostJsonCodec<br/>schema 2.1 JSON"]
        SCHEDULER["BleRequestScheduler<br/>P0-P4 优先级"]
        BLE["BleManager<br/>GATT / MTU / 分片 / 请求响应"]
        SESSION_STORE["SessionPersistenceStore"]
        INBOX_STORE["HostEventInbox"]
        CHECKPOINT_STORE["ProtocolCheckpointStore"]
        REPOSITORIES["Session / Settings Repositories"]
        ROOM[("Room v8")]
        T_SESSIONS["sessions / app_usage / settings"]
        T_RELIABLE["host_event_inbox"]
        T_OUTBOX["sync_outbox"]
        T_CHECKPOINT["protocol_checkpoints"]
    end

    subgraph PLATFORM["Android 平台能力"]
        USAGE["UsageStatsManager"]
        POWER["PowerManager"]
        NOTIFY["NotificationManager"]
        BT["BluetoothGatt"]
        NFC_API["NfcAdapter"]
    end

    HOST["EdgeFocus 主机<br/>schema 2.1"]

    NFC --> MAIN
    NFC_API --> MAIN
    MAIN --> NAV
    MAIN --> OVERLAY
    MAIN --> SAVER
    NAV --> HOME
    NAV --> DASH
    NAV --> SETTINGS
    HOME --> PANEL
    MAIN --> PANEL
    PANEL --> PAGER
    PANEL --> SAVER
    PAGER --> PAGE1
    PAGER --> PAGE2
    PAGER --> PAGE3
    HOME --> VM
    DASH --> VM
    SETTINGS --> VM
    PAGER --> VM

    APP --> SESSION
    APP --> ALERT
    APP --> CONNECTION
    VM --> SESSION
    VM --> TIMER
    VM --> PRECHECK
    VM --> REPOSITORIES
    ALERT --> OVERLAY
    ALERT --> NOTIFY

    SESSION --> SESSION_MODEL
    SESSION --> TIMER
    SESSION --> SESSION_STORE
    SESSION --> MONITOR
    ALERT --> EVENT_POLICY
    ALERT --> INBOX_STORE
    PRECHECK --> PRECHECK_POLICY
    PRECHECK --> CHECKPOINT_STORE
    MONITOR --> PHONE_POLICY
    MONITOR --> CHECKPOINT_STORE
    MONITOR --> USAGE
    MONITOR --> POWER

    CONNECTION --> CODEC
    CONNECTION --> BLE
    TIMER --> BLE
    ALERT --> BLE
    PRECHECK --> BLE
    MONITOR --> BLE
    BLE --> CODEC
    BLE --> SCHEDULER
    BLE --> BT
    BT <--> HOST

    SESSION_STORE --> ROOM
    INBOX_STORE --> ROOM
    CHECKPOINT_STORE --> ROOM
    REPOSITORIES --> ROOM
    ROOM --> T_SESSIONS
    ROOM --> T_RELIABLE
    ROOM --> T_OUTBOX
    ROOM --> T_CHECKPOINT
```

## 2. 页面架构

```mermaid
flowchart LR
    subgraph PORTRAIT["MainActivity · 竖屏"]
        NAV["NavigationGraph"]
        HOME["首页<br/>圆形计时器"]
        DASH["统计"]
        SETTINGS["设置"]
        BLE["设备连接"]
        APPS["分心应用"]
        TEST["开发者工具<br/>Debug only"]
        NAV --> HOME
        NAV --> DASH
        NAV --> SETTINGS
        SETTINGS --> BLE
        SETTINGS --> APPS
        SETTINGS --> TEST
    end

    subgraph LANDSCAPE["ControlPanelActivity · 横屏"]
        PAGER["独立 HorizontalPager"]
        P1["Page 1<br/>计时 / PRECHECK<br/>图标式控制"]
        P2["Page 2<br/>当前会话统计<br/>供电 / 手环只读状态"]
        P3["Page 3<br/>主机控制设置"]
        PAGER --> P1
        PAGER --> P2
        PAGER --> P3
    end

    NFC["NFC 标签"] --> LANDSCAPE
    HOME --> LANDSCAPE
```

横屏控制面板是独立页面树，不是竖屏首页旋转后的横版；两个 Activity 读取同一个进程级会话状态。

## 3. 核心组件关系

| 组件 | 主要职责 | 主要依赖 |
|---|---|---|
| `EdgeFocusApp` | 启动进程级协调器、初始化设置 | Hilt、SettingsDao |
| `AppSessionCoordinator` | 唯一会话、owner 冻结、监控服务、Session/Outbox 保存 | TimerStateHolder、ConnectionSupervisor、SessionPersistenceStore |
| `TimerStateHolder` | 共享计时状态、主机状态映射、页面兼容状态 | BleManager、SettingsDao |
| `ConnectionSupervisor` | GATT 后 hello/state 协商、READY、自动重连 | BleManager、HostJsonCodec |
| `AlertDeliveryCoordinator` | Inbox、TTL、去重、通知/覆盖层、ACK | HostEventInbox、BleManager、NotificationManager |
| `PrecheckController` | 云台 sequence、精确命令、确认位置、等待 FOCUSING | ProtocolCheckpointStore、BleManager |
| `AppMonitorService` | UsageStats 采集、PhoneFact、2 秒心跳 | ProtocolCheckpointStore、BleManager |
| `BleManager` | BLE GATT、MTU、分片、请求 ID 和推送分发 | BluetoothGatt、BleRequestScheduler |
| `HostJsonCodec` | schema 2.1 请求与响应编解码 | org.json |
| `SessionPersistenceStore` | Session 与 Outbox 事务 | Room |
| `HostEventInbox` | 可靠事件状态和 ACK checkpoint | Room、elapsedRealtime、boot count |

## 4. EdgeFocus 会话数据流

```mermaid
sequenceDiagram
    actor User as 用户
    participant UI as 首页或横屏 Page 1
    participant VM as ViewModel
    participant SC as AppSessionCoordinator
    participant TS as TimerStateHolder
    participant CS as ConnectionSupervisor
    participant BLE as BleManager
    participant Host as EdgeFocus 主机
    participant DB as Room

    User->>UI: 点击开始图标
    UI->>VM: startTimer / startSession
    VM->>SC: startEdgeFocus(duration)
    SC->>TS: 建立共享 session_id，冻结 EDGEFOCUS owner
    TS->>BLE: session_control(start, live_camera)
    BLE->>Host: schema 2.1 请求
    Host-->>BLE: PRECHECK snapshot
    BLE-->>TS: 更新共享状态
    TS-->>UI: TimerState.PRECHECK
    User->>UI: 调整云台并确认
    UI->>Host: PRECHECK 精确命令 + sequence
    Host-->>UI: FOCUSING / RUNNING
    SC->>DB: 正式结束报告后保存 Session
```

## 5. LOCAL 会话数据流

```mermaid
sequenceDiagram
    actor User as 用户
    participant UI as 首页或横屏控制页
    participant SC as AppSessionCoordinator
    participant TS as TimerStateHolder
    participant Monitor as AppMonitorService
    participant Store as SessionPersistenceStore
    participant DB as Room
    participant Host as EdgeFocus 主机

    User->>SC: startLocal(duration)
    SC->>TS: 建立 LOCAL session_id
    SC->>Monitor: 启动本地应用监控
    TS-->>UI: RUNNING / PAUSED / FINISHED
    SC->>Store: 保存 Session + SyncOutbox
    Store->>DB: 单个 Room 事务
    SC->>Host: 主机 READY 后上传 Outbox
    Host-->>SC: accepted + board session id
    SC->>DB: 标记 synced 并删除 Outbox
```

## 6. 可靠事件数据流

```mermaid
sequenceDiagram
    participant Host as EdgeFocus 主机
    participant BLE as BleManager
    participant AC as AlertDeliveryCoordinator
    participant Inbox as HostEventInbox
    participant UI as AlertOverlay / Notification

    Host->>BLE: push_alert / push_session_end
    BLE->>AC: ReliableEventEnvelope
    AC->>Inbox: 保存 event_id、TTL、本地 deadline
    AC->>Host: ACK received
    AC->>Inbox: CLAIMED -> EFFECT_STARTED
    AC->>UI: 显示覆盖层或系统通知
    AC->>Inbox: PRESENTED
    AC->>Host: ACK presented
    opt push_alert 用户确认
        UI->>AC: 我知道了
        AC->>Host: ACK user_ack
    end
```

## 7. BLE 协议栈

```mermaid
flowchart LR
    CALLER["Coordinator / Controller / Service"]
    CODEC["HostJsonCodec"]
    QUEUE["BleRequestScheduler"]
    MANAGER["BleManager"]
    CHUNK["BleChunkAssembler"]
    GATT["BluetoothGatt"]
    HOST["EdgeFocus 主机"]

    CALLER --> CODEC
    CODEC --> QUEUE
    QUEUE -->|P0-P4| MANAGER
    MANAGER --> GATT
    GATT <--> HOST
    HOST --> GATT
    GATT --> CHUNK
    CHUNK --> MANAGER
    MANAGER --> CALLER
```

优先级：

- P0：可靠事件 ACK、PRECHECK 关键命令。
- P1：会话控制。
- P2：连接协商和状态读取。
- P3：PhoneFact、Outbox 等后台同步。
- P4：可取消的摄像头预览。

## 8. Room 数据结构

```mermaid
erDiagram
    SESSIONS {
        string id PK
        long startTime
        long endTime
        int duration
        int focusDuration
        string source
        boolean synced
        string boardSessionId
    }

    APP_USAGE {
        long id PK
        string packageName
        string category
        int usageDuration
        string sessionId FK
    }

    HOST_EVENT_INBOX {
        string eventId PK
        string sessionId
        string eventType
        long localDeadlineElapsedMs
        int receivedBootCount
        string dispatchState
        int localAckStage
        int confirmedAckStage
    }

    SYNC_OUTBOX {
        long id PK
        string sessionId UK
        string payloadJson
        int attemptCount
        long nextAttemptAtMs
    }

    PROTOCOL_CHECKPOINTS {
        string sessionId PK
        long phoneSequence
        long gimbalSequence
    }

    SETTINGS {
        int id PK
        int pomodoroDuration
        boolean useEdgeFocusBoard
        boolean screenSaverEnabled
        int screenSaverTimeoutSec
        boolean appScreenAlertsEnabled
        boolean appSoundAlertsEnabled
    }

    SESSIONS ||--o{ APP_USAGE : contains
    SESSIONS ||--o| SYNC_OUTBOX : queues
    SESSIONS ||--o| PROTOCOL_CHECKPOINTS : checkpoints
```

## 9. 源码目录映射

```text
com.edgefocus.app
├── MainActivity.kt
├── ControlPanelActivity.kt
├── EdgeFocusApp.kt
├── presentation
│   ├── navigation
│   ├── ui/home
│   ├── ui/controlpanel
│   ├── ui/dashboard
│   ├── ui/settings
│   ├── shared
│   └── viewmodel
├── domain
│   ├── session
│   ├── alert
│   ├── precheck
│   ├── model
│   └── repository
├── data
│   ├── ble
│   │   └── model
│   ├── remote
│   ├── local
│   │   ├── entity
│   │   └── dao
│   ├── session
│   └── repository
├── service
└── util
```

## 10. 架构原则

1. App 进程只有一个有效会话，竖屏与横屏共享相同 session_id。
2. NFC 只负责进入独立横屏控制面板，不改变 LOCAL/EDGEFOCUS owner。
3. EdgeFocus 会话以主机 runtime snapshot 和正式结束报告为准。
4. LOCAL 会话先可靠保存，再异步通过 Outbox 同步主机。
5. 可靠推送先写 Inbox，再执行通知、声音或振动。
6. 所有 phone/gimbal sequence 在发送前写入 checkpoint。
7. BLE GATT CONNECTED 不等于业务 READY；必须完成 schema 2.1 协商。
8. 主机 capability 未声明的功能只读展示，不在 App 端假设可控制。

