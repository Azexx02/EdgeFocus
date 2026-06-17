# EdgeFocus App ↔ Board 通信协议文档

> **用途**：板端团队开发参考
> **版本**：2026-06-17
> **格式**：所有命令均为 JSON（UTF-8），完整协议见 `Mobile_API_Contract_v2NEW.md`

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
- 连接后手机请求 MTU ≥ 128
- 手机通过设备名称过滤（`EdgeFocus`/`ESP32`），用户可手动绑定 MAC 地址

---

## 三、命令格式

### 请求（手机 → 板）

```json
{"cmd": "<命令名>", "data": {<参数>}}
```

### 响应（板 → 手机）

```json
{"status": "success"|"error", "data": {<结果>}, "error": "<错误信息>"}
```

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
| `focus_state` | int | FocusState 枚举（0-7） |
| `elapsed_ms` | int64 | 已进行毫秒数，无会话时=0 |
| `remaining_ms` | int64 | 剩余毫秒数（手机用于同步倒计时） |
| `target_duration_ms` | int64 | 目标时长毫秒数 |
| `distraction_count` | int | 分心次数（板端综合判定） |
| `reminder_count` | int | 提醒次数 |
| `calibration_completed` | bool | 校准是否完成 |

**手机轮询频率：** 1-2 秒，用于实时同步倒计时和显示状态。

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
| `app_name` | string | 包名或 "com.launcher" |
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
    "total_duration": 2700,
    "focus_duration": 2250,
    "distraction_count": 4,
    "reminder_count": 2
  }
}
```

### 4.7 `get_console_data` — 查询控制台数据

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

### `push_alert` — 干预提醒

```json
{"cmd":"push_alert","data":{"level":2,"message":"学习期间手机正在使用，先放到一边吧。"}}
```

**触发时机：** ReminderPolicy 生成干预命令时

**手机处理：** 控制面板打开时显示提醒弹窗（其他页面不显示）。用户可点击"已了解"发送 `reminder_ack`。

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

**⚠️ 手动停止时也会发 `session_control(stop)` 给板端，板端应回复 `push_session_end`。手机等板端回复后再保存 Session，不提前本地创建。

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
| `duration_sec` | 本次会话总时长（秒） |

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
8. 板端推送 push_alert → 手机显示提醒
9. 计时器完成 → 板端推送 push_timer_complete → 手机记录会话
```

### 场景 B：板端已在专注，手机连接同步

```
1. 手机打开，BLE 连接
2. 连接后自动检测 useEdgeFocusBoard 设置
3. 调用 get_current_state → 板端返回 active=true + elapsed_ms
4. 手机计算 remaining = target - elapsed → 同步倒计时
5. 启动 boardSync（每 2 秒轮询 get_current_state 校准）
6. 启动 phoneSync 上报前台应用
```

### 场景 C：板端主动推送

```
1. 板端推送 push_state(RUNNING)
2. 手机检测到 IDLE + EdgeFocus 模式
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
| 推送事件接收 | ✅ | push_state/push_alert/push_session_end/push_timer_complete |
| BLE 设备绑定（MAC） | ✅ | — |
| 模拟模式 | ✅ | — |
| 统计页数据 | ⏳ | get_today_summary, get_last_report |

---

## 十、板端需实现的事项

| 事项 | 优先级 | 说明 |
|------|--------|------|
| `session_control` 响应中添加 `remaining_ms` 和 `target_duration_ms` | 🔴 高 | 手机计时器依赖此数据启动倒计时 |
| `phone_sync` 响应返回 `focus_state` 和 `action_taken` | 🟡 中 | 板端处理手机干扰后的状态反馈 |
| 所有命令响应统一 JSON 格式 | 🔴 高 | 手机端已全部改为 JSON 发送 |
| `push_session_end` 字段对齐（total_duration, focus_duration 等） | 🟡 中 | 手机端保存会话记录依赖 |
| `get_today_summary` / `get_last_report` 实现 | 🟢 低 | 统计页数据展示 |
| 硬件控制接口（振动/LED/提醒灵敏度） | 🟡 中 | 控制面板设置页需要调用 |
| 离座超时通过 `update_preferences` 发送 | 🟡 中 | 手机端调整后需同步到板端 |
| 系统模式通过 `update_preferences` 发送 | 🟡 中 | 静音/正常模式切换需同步到板端 |

### 硬件控制接口建议

手机端控制面板第3页需要调整以下参数，建议板端新增 `update_preferences` 支持：

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

### 提醒反馈流程

```
板端 ──── push_alert ────→ 手机（显示提醒弹窗）
手机 ──── reminder_ack ────→ 板端（用户点击"已了解"）
板端 ←──── {"status":"success","data":{"cooling_ms":...}} ──── 手机
```

手机收到 `push_alert` 后弹窗，用户点击"已了解"按钮 → 发送 `reminder_ack` → 板端进入冷却期。
