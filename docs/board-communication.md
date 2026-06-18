# EdgeFocus App ↔ Board 通信协议文档

> **用途**：板端团队开发参考
> **版本**：2026-06-17
> **格式**：所有命令均为 JSON（UTF-8），完整协议见 `Mobile_API_Contract_v2NEW.md`

---

## 零、产品定位与本文档效力

EdgeFocus 项目中，手机 App 当前承担两个核心职责：

1. **手机使用状态采集器**：检测用户当前手机使用状态、前台应用类别、干扰等级，并通过 `phone_sync` 提供给 EdgeFocus 设备的 EventDetector/ReminderPolicy 使用。`phone_sync` 是板端判断用户是否被手机干扰的重要输入，但不是最终分心结论；最终结论仍由板端结合摄像头、状态机和提醒策略统一判断。
2. **EdgeFocus 大屏遥控器**：手机屏幕大于产品自带屏幕，因此 App 承担一部分控制面板功能，包括启动/暂停/恢复/结束会话、模式切换、提醒确认、振动/LED/灵敏度/离座超时等参数调节、查看当前状态和统计。

**本地模式不是产品主流程**。本地模式是为了复用番茄钟、监控、会话记录等 App 代码而加入的附属能力；不连接 EdgeFocus 设备时可独立运行，但不代表板端可以缺失核心会话控制、状态同步、报告生成和提醒策略。

当前 App 端开发进度最快，因此本文档作为阶段性主导文档，用于直接指导板端实现。凡本文档标为“必须”的字段、事件、流程，板端按本文档实现，不再等待额外确认。若板端现有代码与本文档冲突，以本文档为用户会话流程和信息交互准则。

---

## 一、项目架构与角色

### 1.1 三个组成部分

#### EdgeFocus 设备（核心）

**绝对核心设备**，承担本项目所有关键功能：

- **视觉识别**：摄像头实时检测用户面部、视线方向、姿态
- **行为分析**：EventDetector 综合视觉数据 + 手机干扰信息，判断用户是否分心
- **干预提醒**：ReminderPolicy 根据分心等级决定提醒策略，选择提醒介质：
  - 本机提示音、灯光提醒、屏幕闪烁
  - BLE Band 振动（静音模式下的主要提醒方式）
  - 具体使用哪种介质由分心等级和当前模式决定
- **会话管理**：启动/暂停/恢复/结束会话，生成会话报告
- **数据存储**：所有会话记录、统计数据以板端为准。**如果有冲突，以板端数据为准**
- **独立运行**：启动会话、调节设置、控制面板等功能本机均可独立操作

#### BLE Band（手环终端）

与板端自带的提示音/灯光/屏幕闪烁**属于同一类**（都是提醒介质），由板端代码根据分心等级决定使用哪种：

- 通常挂靠在 EdgeFocus 上充电，取下后自动连接板端
- **静音模式下的主要提醒方式**
- 通过振动提醒用户
- 几乎不向板端反馈信息
- 需要与板端交流：设备电量信息、确认提醒已生效

#### 手机 App（操作终端）

EdgeFocus 本机可以独立完成大部分功能（启动会话、调节设置、控制面板），手机 App 的优势在于**屏幕素质好且尺寸较大，操作更方便**：

- **启动会话**：可发起专注会话（本机也可独立操作）
- **会话反馈**：对 EdgeFocus 的会话记录进行反馈（提醒过多/过少/适中）
- **个性化调节**：调整硬件参数（振动强度、LED 亮度、灵敏度等）
- **前台应用检测**：检测手机使用情况，上报给 EdgeFocus 用于行为分析
- **控制面板模式**：横屏界面，功能与 EdgeFocus 本机面板类似，接管大部分屏幕操作
- **本地模式**：为提高代码利用率而加入，不连接板端独立运行番茄钟。与本项目核心流程关系不大，数据可选同步给板端

### 1.2 数据优先级

```
EdgeFocus 板端 > 手机 App > BLE Band
```

板端是数据唯一的准则。手机端和手环端的数据以板端为准，冲突时以板端数据为准。

### 1.3 BLE 通信架构

```
手机 App (Central)  ←──BLE GATT──→  EdgeFocus 设备 (Peripheral)
                     Write: 手机→板
                     Notify: 板→手机

EdgeFocus 设备 (Central)  ←──BLE GATT──→  BLE Band (Peripheral)
                     提醒指令: 板→手环
                     电量/确认: 手环→板
```

两路 BLE 独立运行，互不干扰。手机 App 只与 EdgeFocus 设备通信，不与手环直接通信。

---

## 二、BLE 连接

- Service UUID: `0000FF00-0000-1000-8000-00805F9B34FB`
- Write (手机→板): `0000FF01-...`
- Notify (板→手机): `0000FF02-...`
- 连接后手机请求 MTU 517；板端必须支持协商到 MTU 517
- 初次绑定时，App 只展示“广播名称匹配 + 广播包包含 FF00 Service UUID”的设备；绑定后按稳定 MAC 地址精确重连

### 2.1 板端广播与识别要求

板端必须作为 **BLE Peripheral / GATT Server** 发送可连接的低功耗蓝牙广播。仅开启经典蓝牙、仅修改系统蓝牙名称，或者只在连接后的 GATT 表中创建服务，都不足以被 App 识别。

初次绑定列表采用以下两个条件的交集：

1. 广播 Local Name 包含 `EdgeFocus`（推荐使用 `EdgeFocus` 或 `EdgeFocus-XXXX`）；当前为兼容开发固件也接受包含 `ESP32` 的名称。
2. Advertising Data 或 Scan Response 的 Service UUID 列表中包含 `0000FF00-0000-1000-8000-00805F9B34FB`。不能只创建 FF00 GATT Service 而不在广播数据中声明它。

板端最低广播要求：

| 项目 | 必须值/行为 |
|------|-------------|
| BLE 角色 | Peripheral + GATT Server |
| 广播类型 | Connectable、Scannable |
| Local Name | `EdgeFocus` 或 `EdgeFocus-XXXX`（开发期兼容 `ESP32`） |
| 广播 Service UUID | `0000FF00-0000-1000-8000-00805F9B34FB` |
| 地址策略 | 使用稳定的 Public Address 或 Static Random Address；禁止使用会轮换的 RPA |
| 广播持续性 | 未连接时持续广播；断连后恢复广播 |
| 建议广播间隔 | 100-500 ms，兼顾发现速度和功耗 |

用户确认绑定后，App 保存设备 MAC。后续扫描优先按该 MAC 精确识别并自动连接，不再依赖名称；因此板端固件升级和重启后必须保持 BLE 地址稳定。若板端启用地址隐私导致 MAC 轮换，App 会把它视为另一台设备，原绑定将无法重连。

另一台手机即使把“系统蓝牙名称”改成 EdgeFocus，通常也不会出现在列表，因为手机默认不是发送上述可连接 BLE 广播的 Peripheral，而且不会广播 FF00 Service UUID。名称本身不是设备认证；FF00 广播用于产品类型筛选，已绑定 MAC 用于后续锁定具体设备。

---

## 三、命令格式

### 请求（手机 → 板）

```json
{"cmd": "<命令名>", "id": 12, "data": {<参数>}}
```

### 响应（板 → 手机）

```json
{"status": "success"|"error", "id": 12, "data": {<结果>}, "error": "<错误信息>"}
```

**强制规则：** App 实际发送的每个普通请求都会带 `id`；板端普通响应必须原样回显同一个 `id`。下文若有示例省略 `id`，仅为阅读简化，板端实现时不能省略。

### 推送（板 → 手机，无需手机请求）

```json
{"cmd": "push_<事件名>", "data": {<事件数据>}}
```

---

## 四、命令详细说明

### 4.1 `session_control` — 会话控制

**手机发送：**
```json
{"cmd":"session_control","data":{"action":"start","target_duration_min":25}}
```

| action | 含义 |
|--------|------|
| `start` | 开始新会话 |
| `pause` | 暂停 |
| `resume` | 恢复 |
| `end` | 结束会话 |

**⚠️ 板端必须在响应中返回时间字段：**

```json
{
  "status": "success",
  "data": {
    "success": true,
    "remaining_ms": 1500000,
    "target_duration_ms": 1500000
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | bool | 操作是否成功 |
| `remaining_ms` | int64 | 会话剩余毫秒数（板端启动后立即计算） |
| `target_duration_ms` | int64 | 目标时长毫秒数 |

**为什么需要时间字段：** 手机收到 `session_control` 响应后，用 `remaining_ms` 启动本地倒计时。如果板端不返回时间，手机只能用本地时间，可能和板端不同步。

### 4.2 `get_current_state` — 查询当前状态

**手机发送：** `{"cmd":"get_current_state"}`

**板端响应：**
```json
{
  "status": "success",
  "data": {
    "active": true,
    "session_id": "session_1710000000000",
    "focus_state": 1,
    "elapsed_ms": 1205000,
    "remaining_ms": 295000,
    "target_duration_ms": 1500000,
    "distraction_count": 3,
    "reminder_count": 1,
    "calibration_completed": true
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `active` | bool | 当前是否有活跃会话（手机据此判断是否需要同步） |
| `session_id` | string | 板端会话唯一 ID；手机中途接入时用于关联同一会话 |
| `focus_state` | int | FocusState 枚举（0-7） |
| `elapsed_ms` | int64 | 已进行毫秒数，无会话时=0 |
| `remaining_ms` | int64 | 剩余毫秒数（手机用于同步倒计时） |
| `target_duration_ms` | int64 | 目标时长毫秒数 |
| `distraction_count` | int | 分心次数（板端综合判定） |
| `reminder_count` | int | 提醒次数 |
| `calibration_completed` | bool | 校准是否完成 |

**手机轮询频率：** 1-2 秒，用于实时同步倒计时和显示状态。

**会话活跃判定：** `focus_state=0..5` 或 `7` 都表示会话仍在运行，只有 `6` 表示暂停。板端推送正常专注态时必须发送 `0=FOCUSED`，App 会将这些业务状态统一映射为计时器 RUNNING，而不是只把某一个干扰状态当作 RUNNING。

**⚠️ 手机依赖 `remaining_ms` 和 `target_duration_ms` 来同步倒计时，请板端确保返回。**

### 4.3 `phone_sync` — 上报手机使用状态

**手机发送：**
```json
{
  "cmd": "phone_sync",
  "data": {
    "is_in_use": true,
    "app_category": "entertainment",
    "distraction_level": 2,
    "app_name": "com.ss.android.ugc.aweme",
    "duration_sec": 120
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `is_in_use` | bool | 手机是否在使用 |
| `app_category` | string | learning/social/entertainment/other |
| `distraction_level` | int | 0=学习, 1=弱干扰, 2=强干扰, 3=未分类 |
| `app_name` | string | 包名；桌面/后台且系统未提供 Launcher 前台事件时为 `"desktop_or_background"` |
| `duration_sec` | int | 当前应用使用时长（秒） |

**板端响应（需返回）：**
```json
{"status":"success","data":{"focus_state":3,"action_taken":true}}
```

| 字段 | 说明 |
|------|------|
| `focus_state` | 处理后的 FocusState |
| `action_taken` | 是否触发了状态切换 |

**触发时机：** 前台应用切换时发送一次，不重复。

### 4.4 `get_device_status` — 查询设备状态

**手机发送：** `{"cmd":"get_device_status"}`

**板端响应：**
```json
{
  "status":"success",
  "data":{
    "battery_percent": 85,
    "system_mode": 0,
    "camera_ok": true,
    "bracelet_connected": false,
    "is_session_active": true
  }
}
```

### 4.5 `get_today_summary` — 查询今日统计

**手机发送：** `{"cmd":"get_today_summary"}`

**板端响应：**
```json
{
  "status":"success",
  "data":{
    "total_seconds": 7200,
    "focus_seconds": 6000,
    "sessions": 3,
    "events": 5
  }
}
```

### 4.6 `get_last_report` — 查询上次报告

**手机发送：** `{"cmd":"get_last_report"}`

**板端响应：**
```json
{
  "status":"success",
  "data":{
    "session_id":"session_1710000000000",
    "total_duration": 2700,
    "focus_duration": 2250,
    "distraction_count": 4,
    "reminder_count": 2,
    "phone_distraction_count": 1,
    "feedback":"just_right"
  }
}
```

### 4.7 `get_session_history` — 分页查询权威会话记录

手机每次只请求一条，保证响应可放入单个 MTU 517 Notify：

```json
{"cmd":"get_session_history","data":{"cursor":"opaque_cursor","limit":1}}
```

首次请求省略 `cursor`。板端按结束时间从新到旧返回，并生成不透明游标：

```json
{
  "status":"success",
  "data":{
    "sessions":[{
      "session_id":"session_1710000000000",
      "start_time_ms":1710000000000,
      "end_time_ms":1710002700000,
      "total_duration":2700,
      "focus_duration":2250,
      "distraction_count":4,
      "reminder_count":2,
      "phone_distraction_count":1,
      "feedback":"just_right"
    }],
    "next_cursor":"opaque_cursor_2",
    "has_more":true
  }
}
```

游标必须稳定指向下一条记录，同一页重试返回相同结果。App 以 `session_id` 覆盖本地缓存，因此板端修改后的报告和反馈可以纠正手机缓存。板端即使收到大于 1 的 `limit` 也应暂时按 1 截断，直到双方实现跨 Notify 分片协议。

### 4.8 `get_preferences` — 查询设备当前设置

**手机发送：** `{"cmd":"get_preferences"}`

```json
{
  "status":"success",
  "data":{
    "system_mode":0,
    "away_timeout_min":15,
    "vibration_strength":0.5,
    "led_brightness":75,
    "reminder_sensitivity":0.5
  }
}
```

控制面板在成功读取前禁用设置操作，避免 App 默认值覆盖设备真实配置。字段范围必须与 `update_preferences` 完全一致。

### 4.9 `get_console_data` — 查询控制台数据

**手机发送：** `{"cmd":"get_console_data"}`

**板端响应：** 一次性返回所有面板数据（session/device/reminder/today）。

---

## 五、板端主动推送事件

板端通过 Notify 推送，手机订阅后自动接收，**手机无需回复**。

### `push_state` — 状态变化

```json
{"cmd":"push_state","data":{"focus_state":3,"elapsed_ms":1205000,"distraction_count":4}}
```

**触发时机：** FocusState 发生切换时

**手机处理：** 更新 UI 状态显示

### `push_session_end` — 会话结束

```json
{
  "cmd":"push_session_end",
  "data":{
    "total_duration": 2700,
    "focus_duration": 2250,
    "distraction_count": 4,
    "reminder_count": 2
  }
}
```

**触发时机：** 会话结束 + 报告生成后

**手机处理：** 保存会话记录到本地数据库（`synced=true`，板端数据为权威来源）。如果手机未收到 `push_timer_complete`，则用此事件的数据保存。

**⚠️ 手动停止时 App 内部调用 stop，但 BLE 协议实际发送 `session_control` 的 `action=end` 给板端，板端应回复 `push_session_end`。手机会立即停止本机 App 监控，但 Session 记录优先等板端 `push_session_end` 或 `session_control(action=end)` 响应中的报告字段后保存；如果 BLE 命令失败、断连，或 stop 成功后板端长时间没有返回报告，手机会用本地计时兜底保存，并标记为未同步。

### `push_timer_complete` — 计时器完成

```json
{
  "cmd":"push_timer_complete",
  "data":{
    "elapsed_ms": 1500000,
    "target_duration_ms": 1500000,
    "distraction_count": 3,
    "reminder_count": 1
  }
}
```

**触发条件：** `elapsed_ms >= target_duration_ms` 且 `target_duration_ms > 0`

**手机处理：** 更新干扰/提醒计数，触发本地提醒。**如果手机在计时器完成时收到此事件，用此数据保存 Session 记录（优先级高于 `push_session_end`）。**

### 两个事件的区别

| | push_session_end | push_timer_complete |
|---|---|---|
| 时长字段 | total_duration, focus_duration | elapsed_ms, target_duration_ms |
| 干扰/提醒 | distraction_count, reminder_count | distraction_count, reminder_count |
| 触发时机 | 会话结束+报告生成后 | 计时器到达目标时间时 |
| 手机用途 | 保存 Session | 更新实时计数 + 保存 Session（优先） |

---

## 六、Local 模式会话同步

手机在本地模式完成会话后，如果 BLE 已连接，通过 `phone_sync` 将会话数据同步给板端：

```
手机 ──── phone_sync ────→ 板端
```

**请求格式：**
```json
{
  "cmd": "phone_sync",
  "data": {
    "is_in_use": false,
    "app_category": "other",
    "distraction_level": 3,
    "app_name": "local_session_sync",
    "duration_sec": 1500
  }
}
```

| 字段 | 说明 |
|------|------|
| `is_in_use` | 固定 false（会话已结束） |
| `app_category` | 固定 "other" |
| `distraction_level` | 固定 3 |
| `app_name` | 固定 "local_session_sync"（标识这是本地会话同步） |
| `duration_sec` | 本次会话实际进行时长（秒）；手动停止时不是目标时长 |

**板端处理：**
- 收到 `app_name == "local_session_sync"` 时，识别为本地会话同步数据
- 累加到今日统计（`focus_seconds += duration_sec`）
- 手机干扰次数由板端从实时 `phone_sync` 上报中统计

---

## 七、数据同步机制

### 手机干扰上报 → 板端计数

```
手机检测到干扰App → phone_sync(is_in_use=true, level=2, pkg="xxx")
                                                    ↓
板端 EventDetector 接收 → 结合摄像头画面判断是否分心 → 累加 distraction_count
```

**关键点**：`phone_sync` 不直接等于分心。板端 EventDetector 综合手机使用 + 摄像头画面判定。

### 干扰数据含义

| 数据 | 含义 | 来源 |
|------|------|------|
| `distraction_count` | 分心次数 | 板端 EventDetector（摄像头+手机干扰权重） |
| `reminder_count` | 提醒次数 | 板端 ReminderPolicy |
| `phone_distraction_count` | 手机干扰次数 | 板端统计（手机干扰导致分心的次数） |

### 手机端 Session 记录

手机本地数据库存储会话记录，字段说明：

| 字段 | 说明 |
|------|------|
| `source` | 数据来源：`"LOCAL"`（本地模式）或 `"EDGEFOCUS"`（板端同步） |
| `feedback` | 用户反馈：`"too_much"` / `"too_little"` / `"just_right"` 或 null |
| `duration` | 会话时长（秒），由 `totalTime - timeRemaining` 计算 |
| `distractionCount` | EdgeFocus 模式：板端 push_timer_complete 值；LOCAL 模式：0 |
| `reminderCount` | EdgeFocus 模式：板端 push_timer_complete 值；LOCAL 模式：0 |
| `phoneDistractionCount` | 手机干扰次数（level>0 的 phone_sync 发送次数） |
| `boardSessionId` | 板端会话 ID（EdgeFocus 模式） |

| 字段 | EdgeFocus 模式 | 本地模式 |
|------|---------------|---------|
| duration | totalTime - timeRemaining | 同左 |
| distractionCount | 板端 push_timer_complete.distraction_count | 0 |
| reminderCount | 板端 push_timer_complete.reminder_count | 0 |
| phoneDistractionCount | 板端管理 | 手机计数 phone_sync 发送次数（level>0） |

---

## 八、工作场景

### 场景 A：手机启动 EdgeFocus 专注

```
1. 用户在 HomeScreen 选时长 → 点击 Start
2. 手机 → 板端：session_control(start, duration=25)
3. 板端 → 手机：响应 success + remaining_ms + target_duration_ms
4. 手机用 remaining_ms 启动本地倒计时
5. 手机每 1-2 秒轮询 get_current_state 同步板端时间
6. 手机每 1 秒检测前台应用 → phone_sync 上报当前 app
7. 板端 EventDetector 判断分心 → distraction_count++
8. 板端通过设备自身的声音、灯光或振动执行提醒，并累加 `reminder_count`
9. 计时器完成 → 板端推送 push_timer_complete → 手机记录会话
```

### 场景 B：板端已在专注，手机连接同步

```
1. 手机打开，BLE 连接
2. 首页 EdgeFocus 模式按设置检测；NFC 控制面板始终按 EdgeFocus 模式检测
3. 调用 get_current_state → 板端返回 active=true + session_id + focus_state + elapsed_ms
4. 手机计算 remaining = target - elapsed → 同步倒计时
5. 启动 boardSync（每 2 秒轮询 get_current_state 校准）
6. 启动 phoneSync 上报前台应用
```

### 场景 C：板端主动推送

```
1. 板端推送 push_state(RUNNING)
2. 手机检测到 IDLE/FINISHED + EdgeFocus 模式；若原为 PAUSED，则直接恢复并重新同步
3. 调用 get_current_state → 同步板端时间
4. 启动倒计时 + phoneSync 上报
```

### 场景 D：手机本地专注（不连接板端）

```
1. 用户在 HomeScreen 选时长 → 点击 Start（本地模式）
2. 手机本地倒计时，不发送任何 BLE 命令
3. 手机每 1 秒检测前台应用 → phone_sync 不发送（无板端）
4. 手机自己计数干扰次数
5. 计时器完成 → 保存 Session（distractionCount=0, reminderCount=0）
```

---

## 九、手机端已完成的实现

| 功能 | 状态 | 命令 |
|------|------|------|
| 前台应用检测+上报 | ✅ | phone_sync |
| 会话控制 | ✅ | session_control |
| 状态轮询 | ✅ | get_current_state |
| 推送事件接收 | ✅ | push_state/push_session_end/push_timer_complete |
| BLE 设备绑定（MAC） | ✅ | — |
| 模拟模式 | ✅ | — |
| 统计页数据 | ⏳ | get_today_summary, get_last_report |

---

## 十、App 主导的板端实现要求

> 背景：当前 EdgeFocus 设备端代码开发较混乱，App 端作为阶段性主导方定义用户会话流程和信息交互边界。以下内容是板端实现依据，不是待讨论建议；板端不要按旧契约或隐含行为实现。

### 10.1 App 端已经采用的协议规则

| 事项 | 优先级 | 当前风险 | 目标行为 |
|------|--------|----------|----------|
| BLE 请求/响应模型 | 🔴 高 | App 端已改为请求带 `id`，写入成功只代表传输确认 | 板端必须通过 Notify 返回同 `id` 的 JSON 响应；无 `id` 的 Notify 只按 push 事件处理 |
| UUID 对齐 | 🔴 高 | App 端已改为 `FF00/FF01/FF02` | 双方统一使用 `0000FF00` Service、`0000FF01` Write、`0000FF02` Notify |
| MTU 协商 | 🔴 高 | App 端在 GATT 连接成功后请求 MTU=517 | 板端必须支持 MTU 517；`get_session_history` 的单条完整记录依赖最多 514 字节有效载荷 |
| 绑定后连接判断 | 🟡 中 | App 端要求用户先绑定特定设备再使用，因此连接判断必须以已绑定 MAC 为准 | 绑定流程必须保证 MAC 保存到本地设置，并在 BLE 层连接/重连前恢复 `targetMacAddress` |
| `update_preferences` 数据类型 | 🟡 中 | App 端已按 key 转换数值类型 | `system_mode`、`away_timeout_min`、`led_brightness` 发送数字；`vibration_strength`、`reminder_sensitivity` 发送浮点数 |
| 会话保存时机 | 🔴 高 | EdgeFocus 模式下若 App 提前用本地计时结果保存 Session，会和板端权威数据冲突 | EdgeFocus 模式优先用 `push_timer_complete`、`push_session_end` 或 `session_control(action=end)` 响应报告保存；手动停止必须保存 Session，若板端没有及时给报告则 App 用本地数据兜底保存为未同步 |

### 10.2 板端必须支持的信息交互

EdgeFocus 模式下，用户会话的权威生命周期在板端：App 可以发起控制、显示状态、上报手机使用情况，但会话是否开始、是否结束、最终报告数据均以板端响应和板端 push 为准。板端必须保证每次会话结束后能通过 `push_timer_complete` 或 `push_session_end` 收口，手动结束则必须通过 `session_control(action=end)` 的响应报告字段或随后的 `push_session_end` 收口。

| 交互 | 板端要求 | App 端用途 |
|------|----------|------------|
| `session_control(start)` 响应 | 必须返回 `success`、`session_id`、`remaining_ms`、`target_duration_ms` | App 根据板端会话 ID 和时间启动倒计时，避免本地与板端生成两份记录 |
| `get_current_state` 响应 | 必须返回 `active`、`session_id`、`focus_state`、`elapsed_ms`、`remaining_ms`、`target_duration_ms`、`distraction_count`、`reminder_count` | App 连接后判断板端是否已有会话，并进行 1-2 秒周期校准 |
| `push_timer_complete` | 时间到时主动推送，字段包含 `elapsed_ms`、`target_duration_ms`、`distraction_count`、`reminder_count` | App 触发完成态、停止监控、保存会话；优先级高于 `push_session_end` |
| `push_session_end` | 会话结束并生成报告后推送，字段必须包含 `total_duration`、`focus_duration`、`distraction_count`、`reminder_count`；如果板端统计手机干扰和会话 ID，也必须包含 `phone_distraction_count`、`session_id` | App 保存板端权威 Session；手动停止也依赖此事件收尾 |
| `phone_sync` 响应 | 必须返回 `focus_state`、`action_taken`，可返回 `distraction_count`、`reminder_count` | App 展示板端处理后的状态；注意 `phone_sync` 本身不等于分心结论 |
| `session_feedback` | 按 `session_id` 更新持久化会话的反馈；只接受 `too_much`、`too_little`、`just_right`，重复写入必须幂等 | 用户修改单次会话反馈；断连或失败时 App 持久化排队并在重连后重试 |
| `get_session_history` | 从持久化存储按游标返回权威会话；当前每页固定 1 条 | App 补回未打开手机期间产生的会话，并校正本地缓存 |
| `get_preferences` | 返回当前已持久化的全部可调设备设置 | 控制面板先读取后允许修改，避免默认值误覆盖 |

#### 10.2.1 `session_feedback` — 保存会话反馈

手机发送：

```json
{"cmd":"session_feedback","data":{"session_id":"session_1710000000000","feedback":"just_right"}}
```

板端必须在成功写入持久化存储后才返回：

```json
{"status":"success","id":12,"data":{"success":true}}
```

每个 EdgeFocus 会话创建时，板端应将反馈默认初始化为 `just_right`（适中）。用户不操作即保留该值；之后 App 发送 `too_much`、`too_little` 或 `just_right` 时覆盖同一会话字段。未知 `session_id` 或非法反馈值必须返回明确错误，不得悄悄创建孤立记录。

#### 10.2.2 板端权威记录与持久化要求

板端不是临时计算节点，而是 EdgeFocus 模式的最终权威数据源。以下数据不能只保存在 RAM 中：

| 记录类别 | 至少保存的内容 |
|----------|----------------|
| 会话主记录 | `session_id`、开始/结束时间、目标时长、实际时长、专注时长、最终状态、结束原因（自然完成/手机结束/板端结束/异常中断） |
| 会话统计 | `distraction_count`、`reminder_count`、`phone_distraction_count`、专注评分或其他最终统计 |
| 状态与关键事件 | 暂停/恢复、离座、分心判定、实际提醒、手机干扰输入等事件的时间点或可还原的时间段摘要 |
| 手机使用摘要 | 会话内各 `app_name` 的使用时长及 App 上报的类别/干扰等级；分类结论仍以 App 上报字段为准 |
| 用户反馈 | 默认 `just_right`，以及后续通过 `session_feedback` 写入的修改值和更新时间 |
| 设备设置 | 振动、LED、提醒灵敏度、离座超时、系统模式等最后一次有效配置 |

持久化规则：

1. `session_control(start)` 成功前先生成全局唯一且稳定的 `session_id`，创建会话记录后再向 App 返回成功。
2. 开始、暂停、恢复、计数变化和结束时都要写入检查点；长会话还应周期性落盘，避免掉电丢失整段数据。
3. 会话结束时先冻结统计并原子写入最终报告，再发送 `push_timer_complete`/`push_session_end`。不得先通知 App、后尝试保存。
4. 重启后必须从持久化记录恢复未结束会话，或将无法恢复的会话以“异常中断”收口；不能把历史和计数清零后伪装成新会话。
5. `get_current_state`、`get_today_summary`、`get_last_report` 以及未来的历史查询必须读取同一份持久化数据，不能各自维护互相冲突的缓存。
6. 相同 `session_id` 的重复开始、结束、反馈和重试请求必须幂等。App 本地 Session 只是显示缓存和断连兜底，不能反向覆盖板端已有的权威报告。
7. 存储空间有限时可压缩或汇总高频原始事件，但必须保留会话主记录、最终报告、反馈和可解释关键事件；清理策略应按时间/容量明确实现，不能在重启时无条件清空。

### 10.3 桌面/后台状态不要依赖 Launcher 包名

Android 不保证桌面一定会以 Launcher 包名出现在 UsageEvents 中。App 端规则如下：

- 如果能拿到真实前台包名：`phone_sync.app_name` 上报真实包名。
- 如果只检测到上一个 App 已退后台，但没有新的前台 App：视为桌面/后台，上报 `app_name="desktop_or_background"`、`app_category="other"`、`distraction_level=3`。
- 板端不需要解析 `app_name` 做学习/干扰分类，分类只以 App 上报的 `app_category` 和 `distraction_level` 为准；`desktop_or_background` 也只是包名占位，供以后做 App 时长统计时识别“没有明确前台包名”的状态。

### 10.4 推送和普通响应必须区分

板端通过同一个 Notify 通道返回普通响应和主动推送，因此消息必须按以下规则区分：

```json
{"status":"success","id":12,"data":{}}   // 普通响应：必须带回请求 id
{"cmd":"push_state","data":{}}           // 主动推送：无 status/id，使用 cmd=push_xxx
```

App 端处理规则：

- 带 `status` 且带 `id`：唤醒对应挂起请求。
- `cmd` 以 `push_` 开头：广播给 PushEventHandler。
- 既无 `status` 又无 `push_`：记录协议错误日志，不崩溃。

当前 App 按“一次 CharacteristicChanged 对应一份完整 UTF-8 JSON”解析，不做跨 Notify 拼包。板端必须支持并与 App 协商 MTU 517；每条响应或推送仍须控制在单包有效载荷（最多 514 字节）内。`get_session_history` 因此固定每页 1 条。若未来数据超过该上限，双方必须先在本文约定长度前缀或分片字段后再实现，不能直接把半段 JSON 发给 App。

---

### 10.5 板端会话状态机实现顺序

板端代码改造时建议先按下面顺序落地，避免 UI、摄像头检测、提醒策略和 BLE 协议互相抢状态：

1. 先实现 BLE GATT 基础层：`FF00` Service、`FF01` Write、`FF02` Notify、CCCD 通知订阅。App 请求 MTU 517，板端必须支持协商到 MTU 517，保证单条历史记录可以完整返回。
2. 再实现统一消息分发：解析 `cmd` 和 `id`，所有普通命令必须通过 Notify 返回同 `id` 响应。
3. 再实现会话状态机：`IDLE -> RUNNING -> PAUSED -> RUNNING -> ENDING -> IDLE`。`ENDING` 阶段必须生成报告，再发 `push_session_end`。
4. 再接入 `get_current_state`：任何时刻都能返回 `active`、`focus_state`、`elapsed_ms`、`remaining_ms`、`target_duration_ms`。
5. 再接入 `phone_sync`：只更新手机使用输入，不直接等同于最终分心结论。
6. 接入会话持久化、历史统计和 `session_feedback`，所有查询都读取同一份权威记录。
7. 最后接入提醒和硬件控制：板端自行执行提醒并维护 `reminder_count`，手机通过 `update_preferences` 调整策略参数。

### 10.6 信号冲突处理规则

| 冲突场景 | 板端必须怎么做 | App 端当前处理 |
|---|---|---|
| App 发 `session_control(start)`，但板端已有活动会话 | 返回 `status=success`、`success=true`，并返回当前会话 `remaining_ms`/`target_duration_ms`；不要重新创建第二个会话 | App 会按板端时间同步 |
| App 发 `session_control(start)`，板端拒绝启动 | 返回 `status=success`、`id`、`data.success=false`，可附带 `message`；不要返回伪造时间 | App 保持 IDLE，不进入 RUNNING |
| App 发 `session_control(end)` 后报告尚未生成 | 立即返回同 `id` 的 `success=true`，随后尽快 `push_session_end`；如果能同步生成报告，也可直接在响应 `data` 中带 `total_duration` 等字段 | App 先停止手机监控，等待报告；超时则本地兜底保存为未同步 |
| 计时自然完成 | 先停止计时并冻结统计，再发送 `push_timer_complete` 或 `push_session_end`；不要重复累加同一次完成 | App 的完成处理是幂等的，但板端仍应避免重复 push |
| `phone_sync` 与摄像头判断冲突 | 以板端 EventDetector 综合结果为准；`phone_sync` 只是手机侧输入 | App 保存板端 `distraction_count`/`reminder_count` 为权威值 |
| Notify 中同时存在普通响应和主动事件 | 普通响应必须带 `status` + `id`；主动事件必须使用 `cmd=push_xxx` 且不要复用请求 `id` | App 按 `status/id` 与 `push_` 分流 |
| App 连接后立即发命令 | 板端必须等 CCCD 订阅成功后再依赖 Notify 返回响应 | App 现在只在通知订阅完成后标记 CONNECTED |

### 10.7 `phone_sync` 会话边界

App 在每次 AppMonitorService 开始时都会重置上报去重键。因此新一轮会话即使用户仍停留在同一个手机 App，也会重新发送第一次 `phone_sync`。板端统计手机干扰时应按“当前板端活动会话”归属，不要把上一轮会话的手机状态延续到下一轮。

如果板端维护 App 使用时长统计，可以使用 `app_name` 做包名维度统计；但学习/干扰分类必须使用 App 上报的 `app_category` 和 `distraction_level`。

## 十一、板端需实现的事项

| 事项 | 优先级 | 说明 |
|------|--------|------|
| `session_control` 响应中添加 `remaining_ms` 和 `target_duration_ms` | 🔴 高 | 手机计时器依赖此数据启动倒计时 |
| `phone_sync` 响应返回 `focus_state` 和 `action_taken` | 🟡 中 | 板端处理手机干扰后的状态反馈 |
| 所有命令响应统一 JSON 格式 | 🔴 高 | 手机端已全部改为 JSON 发送 |
| 所有普通响应回显请求 `id` | 🔴 高 | App 通过 `id` 匹配挂起命令；无 `id` 的 `status` 响应会被忽略 |
| CCCD/Notify 订阅成功后再视为可通信 | 🔴 高 | App 只有收到通知订阅成功后才认为 CONNECTED |
| `session_control(start)` 明确返回 `data.success` | 🔴 高 | `success=false` 时 App 保持 IDLE，不启动本地倒计时 |
| 会话 ID 与记录持久化 | 🔴 高 | `start`、当前状态、最终报告和反馈必须使用同一个稳定 `session_id`，重启后不可丢失 |
| `session_control(end)` 必须收口报告 | 🔴 高 | 响应可直接带报告，或随后 `push_session_end`；否则 App 超时兜底为未同步 |
| `push_session_end` 字段对齐（total_duration, focus_duration 等） | 🟡 中 | 手机端保存会话记录依赖 |
| `get_today_summary` / `get_last_report` 实现 | 🟢 低 | 统计页数据展示 |
| `get_session_history` 游标分页 | 🔴 高 | App 必须补回未打开手机期间由板端独立产生的会话；当前固定每页 1 条 |
| `get_preferences` | 🟡 中 | 控制面板必须先读取板端实际设置再允许修改 |
| `session_feedback` 持久化与幂等更新 | 🟡 中 | 默认适中；App 断连后会重试用户修改 |
| 硬件控制接口（振动/LED/提醒灵敏度） | 🟡 中 | 控制面板设置页需要调用 |
| 离座超时通过 `update_preferences` 发送 | 🟡 中 | 手机端调整后需同步到板端 |
| 系统模式通过 `update_preferences` 发送 | 🟡 中 | 静音/正常模式切换需同步到板端 |

### 硬件控制接口要求

手机端控制面板第3页需要调整以下参数，板端必须通过 `update_preferences` 支持：

```json
{"cmd":"update_preferences","data":{
  "vibration_strength": 0.5,
  "led_brightness": 75,
  "reminder_sensitivity": 0.5
}}
```

| 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|
| `vibration_strength` | float | 0.0-1.0 | 振动强度 |
| `led_brightness` | int | 0-100 | LED 亮度百分比 |
| `reminder_sensitivity` | float | 0.0-1.0 | 提醒灵敏度（越高越容易触发） |

### 提醒执行流程

提醒由板端 ReminderPolicy 决策并通过设备自身的声音、灯光或振动执行。每次实际执行后板端累加 `reminder_count`；手机只展示该统计并调整提醒策略参数，不显示提醒弹窗，也不参与提醒确认或冷却状态机。
