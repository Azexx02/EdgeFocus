# EdgeFocus 板端↔手机端 API 契约 v3.0

> **性质**：双方协作契约（Board ↔ Phone），任何修改需双方确认。
> **协议**：BLE GATT，JSON 载荷。
> **最后更新**：2026-06-15

---

## 1. 通信架构

```
┌─────────────┐      BLE GATT Service A       ┌──────────────┐
│  手机 App    │  ◄─────────────────────────►  │  开发板       │
│  (Central)  │   TX: 手机→板 (Write)         │  (Peripheral)│
│             │   RX: 板→手机 (Read/Notify)    │              │
└─────────────┘                                └──────┬───────┘
                                                       │
                                               BLE GATT Service B
                                                       │
                                                ┌──────▼───────┐
                                                │  ESP32 手环   │
                                                └──────────────┘
```

两路 BLE 独立运行，UUID 不同，互不干扰。

---

## 2. BLE 连接参数

### Service 与 Characteristic

| 项目 | 手机↔板 (Service A) | 板↔手环 (Service B) |
|------|---------------------|---------------------|
| Service UUID | `0000FF00-0000-1000-8000-00805F9B34FB` | `0x180C` |
| TX UUID | `0000FF01-...` (Write) | `0x2A56` (Write) |
| RX UUID | `0000FF02-...` (Read/Notify) | — |
| 角色 | 板=Peripheral, 手机=Central | 板=Central, 手环=Peripheral |

### 连接生命周期

```
手机端                                    开发板
  │                                         │
  │  1. 扫描 (Scan for 0xFF00)              │  广播中
  │  ─────────────────────────────────────►  │
  │                                         │
  │  2. 连接 (Connect)                       │
  │  ─────────────────────────────────────►  │
  │                                         │
  │  3. 协商 MTU (≥128)                     │
  │  ─────────────────────────────────────►  │
  │                                         │
  │  4. 发现 Service/Characteristics        │
  │  ─────────────────────────────────────►  │
  │                                         │
  │  5. 订阅 RX Notify                      │
  │  ─────────────────────────────────────►  │
  │                                         │
  │  ◄═════════════ 数据交互 ══════════════►  │
  │                                         │
  │  6. 断开 / 重连                          │
  │  ─────────────────────────────────────►  │
```

### MTU 与分包

- 默认 MTU 23 字节（有效载荷 20 字节）
- **手机端必须**连接后请求 MTU ≥ 128
- 所有 JSON 响应控制在 **200 字节以内**，128 MTU 可单包发送
- 若未来 JSON 超长，开发板在应用层分包（`{"seq":0,"total":2,"data":"..."}`），手机端组装

---

## 3. 消息格式

### 请求（手机 → 板，Write TX）

```json
{"cmd": "<命令名>", "id": <序号>, "data": {<参数>}}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `cmd` | string | 是 | 命令名，见§4 |
| `id` | int | 否 | 请求序号，用于匹配响应；不填则板端不回 id |
| `data` | object | 否 | 命令参数，部分命令不需要 |

### 响应（板 → 手机，Notify RX）

```json
{"status": "success"|"error", "id": <序号>, "data": {<结果>}, "error": "<错误信息>"}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | `"success"` 或 `"error"` |
| `id` | int | 与请求的 id 对应；请求无 id 则此处也无 |
| `data` | object | 成功时返回结果 |
| `error` | string | 失败时返回错误描述，成功时不存在 |

### 主动推送（板 → 手机，Notify RX）

无需手机请求，板端在事件发生时主动推送：

```json
{"cmd": "push_<事件名>", "data": {<事件数据>}}
```

---

## 4. 接口清单

### 4.1 `get_device_status` — 获取设备状态

**实现方：板端**

**请求：**
```json
{"cmd": "get_device_status"}
```

**响应 data：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `battery_percent` | int | 电池百分比，`-1`=未知 |
| `system_mode` | int | 0=NORMAL, 1=SILENT, 2=HOME |
| `camera_ok` | bool | 摄像头是否正常 |
| `bracelet_connected` | bool | 手环 BLE 是否已连接 |
| `is_session_active` | bool | 当前是否有活跃会话 |

**示例：**
```json
{"status":"success","data":{"battery_percent":85,"system_mode":0,"camera_ok":true,"bracelet_connected":false,"is_session_active":true}}
```

**板端数据源：** `SystemStateStore.snapshot().device` + `.session.active`

---

### 4.2 `get_current_state` — 获取当前专注状态

**实现方：板端**

**请求：**
```json
{"cmd": "get_current_state"}
```

**响应 data：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `focus_state` | int | FocusState 枚举值（0-7） |
| `elapsed_ms` | int64 | 会话已进行毫秒数，无会话时=0 |
| `target_duration_ms` | int64 | 目标时长（毫秒），0=无限制 |
| `remaining_ms` | int64 | 剩余时间（毫秒），0=无限制或已超时 |
| `distraction_count` | int | 本次会话累计分心次数 |
| `reminder_count` | int | 本次会话累计提醒次数 |
| `calibration_completed` | bool | 校准是否已完成 |

**示例：**
```json
{"status":"success","data":{"focus_state":0,"elapsed_ms":1205000,"target_duration_ms":1500000,"remaining_ms":295000,"distraction_count":3,"reminder_count":1,"calibration_completed":true}}
```

**板端数据源：** `SystemStateStore.snapshot().session` + `focus_state`

**手机端轮询频率：** 1-2 秒

---

### 4.3 `get_today_summary` — 获取今日统计

**实现方：板端**

**请求：**
```json
{"cmd": "get_today_summary"}
```

**响应 data：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `total_seconds` | int64 | 今日总会话时长（秒） |
| `focus_seconds` | int64 | 今日总专注时长（秒） |
| `sessions` | int | 今日会话次数 |
| `events` | int | 今日分心事件总数 |
| `phone_distraction_count` | int | 今日手机干扰次数 |

**板端依赖：** `TodayStats` 聚合器（待实现）

---

### 4.4 `get_last_report` — 获取上次会话报告

**实现方：板端**

**请求：**
```json
{"cmd": "get_last_report"}
```

**响应 data：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `total_duration` | int | 上次会话总时长（秒） |
| `focus_duration` | int | 上次会话专注时长（秒） |
| `distraction_count` | int | 分心次数 |
| `reminder_count` | int | 提醒次数 |
| `longest_focus_streak_ms` | int64 | 最长连续专注时长（毫秒） |
| `phone_distraction_count` | int | 手机干扰次数 |

**板端依赖：** `ReportGenerator` 接入 `SQLiteStore`（待实现）

**无报告时：** 返回 `data: null`

---

### 4.5 `session_control` — 会话控制

**实现方：板端**

**请求：**
```json
{"cmd": "session_control", "data": {"action": "<动作>", "target_duration_min": <int>}}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action` | string | 是 | start / pause / resume / end |
| `target_duration_min` | int | 否 | 目标时长（分钟），仅 start 有效，不填=无时间限制 |

**action 枚举：**

| 值 | 效果 | 前置条件 |
|----|------|---------|
| `start` | 开始新会话 → CALIBRATING → FOCUSED | 无活跃会话 |
| `pause` | 暂停当前会话 | 会话活跃中 |
| `resume` | 恢复暂停的会话 | 会话已暂停 |
| `end` | 结束会话 + 生成报告 | 会话活跃中 |

**响应 data：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | bool | 操作是否成功 |
| `target_duration_ms` | int64 | 目标时长（毫秒），仅 start 有值，0=无限制 |

**错误场景：**

| 场景 | error 内容 |
|------|-----------|
| 无活跃会话时 pause/resume/end | `"no active session"` |
| 已有活跃会话时 start | `"session already active"` |
| action 值无效 | `"invalid action"` |

**板端依赖：** 需从 `StatusPage` 触摸逻辑中抽出为独立接口

---

### 4.6 `update_preferences` — 修改设备设置

**实现方：板端**

**请求：**
```json
{"cmd": "update_preferences", "data": {"system_mode": <int>, "away_timeout_min": <int>}}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `system_mode` | int | 否 | 0=NORMAL, 1=SILENT, 2=HOME |
| `away_timeout_min` | int | 否 | 离座自动终止时间（分钟），默认 30 |

**响应 data：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `system_mode` | int | 生效后的模式值 |

**板端依赖：** `ModeManager::setMode()` 已就绪

---

### 4.7 `reminder_ack` — 确认提醒

**实现方：板端**

**请求：**
```json
{"cmd": "reminder_ack"}
```

**响应 data：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `cooling_ms` | int64 | 生效后的冷却时间（毫秒） |

**板端依赖：** `ReminderPolicy::onUserAck()` 已就绪

---

### 4.8 `phone_sync` — 上报手机使用状态

**实现方：手机端发起，板端处理**

**请求：**
```json
{"cmd": "phone_sync", "data": {"is_in_use": <bool>, "app_category": "<类别>", "distraction_level": <int>, "app_name": "<应用名>", "duration_sec": <int>}}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `is_in_use` | bool | 是 | true=手机正在被使用, false=已锁屏/放下 |
| `app_category` | string | 否 | entertainment / social / learning / other |
| `distraction_level` | int | 否 | 干扰等级：0=学习, 1=弱干扰, 2=强干扰, 3=未分类（详见下方说明） |
| `app_name` | string | 否 | 应用名称（如"抖音"） |
| `duration_sec` | int | 否 | 本次连续使用秒数 |

**响应 data：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `focus_state` | int | 处理后的 FocusState |
| `action_taken` | bool | 是否触发了状态切换 |

**板端处理逻辑：**
- `phone_sync` 信号注入 `EventDetector`，由行为层状态机按原有权重判定状态
- `distraction_level` 影响信号强度与持续时间权重，但不直接决定 `CommandLevel`
- `is_in_use=false` → 清除手机干扰信号
- 非 FOCUSED 状态 → 仅记录，不切换

**干扰等级说明：**

| `distraction_level` | 含义 | 典型场景 |
|---------------------|------|---------|
| 0 | 学习软件 | 学习类 App，不干扰 |
| 1 | 弱干扰 | 日常聊天软件（可能包含工作/重要信息，也可能闲聊） |
| 2 | 强干扰 | 游戏、短视频等明确娱乐应用 |
| 3 | 未分类/后台 | 番茄钟在后台，或打开的 App 不属于以上分类 |

**行为层权重（状态优先级）：**
```
LOW_CONFIDENCE > AWAY > PHONE_IN_PLAYING > PHONE_IN_USE > PHONE_IN_LEARNING > SLEEPY_OR_SLUMPED > LOOKING_AWAY > FOCUSED
```
`ReminderPolicy` 根据最终 FocusState 按原有策略决定 CommandLevel，手机信号不绕过此逻辑。

**权重备注：**
- `distraction_level=1`（弱干扰）不一定是真正的干扰，可能是工作消息或重要信息
- 板端可考虑：level 1 的信号强度较低，需要更长时间才触发 PHONE_IN_USE 状态
- level 2（强干扰）信号强度高，应快速触发
- level 0（学习）不触发干扰信号
- 具体权重参数待行为层调优时确定

**板端依赖：** `EventDetector` 需接入 `VisionResult.phone`（待实现）

---

### 4.9 `get_console_data` — 控制台模式数据

**实现方：板端**

控制台模式下，手机端一次性获取屏幕显示的所有数据，避免轮询多个接口。

**请求：**
```json
{"cmd": "get_console_data"}
```

**响应 data：**

```json
{
  "session": {
    "active": true,
    "focus_state": 0,
    "focus_state_str": "FOCUSED",
    "elapsed_ms": 1205000,
    "target_duration_ms": 1500000,
    "remaining_ms": 295000,
    "distraction_count": 3,
    "reminder_count": 1,
    "calibration_completed": true
  },
  "device": {
    "battery_percent": 85,
    "system_mode": 0,
    "system_mode_str": "NORMAL",
    "camera_ok": true,
    "bracelet_connected": false
  },
  "reminder": {
    "level": 0,
    "message": "",
    "since_ms": 0
  },
  "frame_quality": {
    "score": 0.85,
    "reason": "quality ok"
  },
  "today": {
    "total_seconds": 7200,
    "focus_seconds": 6000,
    "sessions": 3,
    "events": 5
  }
}
```

**板端数据源：** `SystemStateStore.snapshot()` 全量

**手机端轮询频率：** 控制台模式下 1 秒

---

## 5. 主动推送事件

板端在以下时机通过 Notify 推送，手机端必须订阅 RX Notify。

### `push_state` — 状态变化

**触发：** FocusState 发生切换

```json
{"cmd":"push_state","data":{"focus_state":3,"elapsed_ms":1205000,"distraction_count":4}}
```

### `push_alert` — 干预提醒

**触发：** ReminderPolicy 生成 InterventionCommand

```json
{"cmd":"push_alert","data":{"level":2,"message":"学习期间手机正在使用，先放到一边吧。"}}
```

### `push_session_end` — 会话结束

**触发：** 会话结束 + 报告生成后

```json
{"cmd":"push_session_end","data":{"total_duration":2700,"focus_duration":2250,"distraction_count":4,"reminder_count":2}}
```

### `push_timer_complete` — 番茄钟时间到

**触发：** `elapsed_ms >= target_duration_ms` 且 `target_duration_ms > 0`

```json
{"cmd":"push_timer_complete","data":{"elapsed_ms":1500000,"target_duration_ms":1500000,"distraction_count":3,"reminder_count":1}}
```

---

## 6. 枚举定义（双方共用）

### FocusState

| 值 | 名称 | 手机端建议展示 |
|----|------|--------------|
| 0 | FOCUSED | "专注中" + 绿色 |
| 1 | LOOKING_AWAY | "视线偏离" + 黄色 |
| 2 | SLEEPY_OR_SLUMPED | "疲劳" + 黄色 |
| 3 | PHONE_IN_USE | "手机干扰" + 红色（弱干扰/未分类） |
| 4 | AWAY | "离座" + 灰色 |
| 5 | LOW_CONFIDENCE | "检测中" + 灰色 |
| 6 | PAUSED | "已暂停" + 灰色 |
| 7 | CALIBRATING | "校准中" + 蓝色 |
| 8 | PHONE_IN_LEARNING | "学习中" + 绿色（学习类 App，不干预） |
| 9 | PHONE_IN_PLAYING | "强干扰" + 红色（游戏/短视频，强干预） |

### SystemMode

| 值 | 名称 | 手机端建议展示 |
|----|------|--------------|
| 0 | NORMAL | "正常" |
| 1 | SILENT | "静音" |
| 2 | HOME | "家庭" |

### CommandLevel

| 值 | 名称 | 手机端建议行为 |
|----|------|--------------|
| 0 | NONE | 无 |
| 1 | LEVEL_1 | 轻提示弹窗 |
| 2 | LEVEL_2 | 红色警告弹窗 + "已了解"按钮 |
| 3 | LEVEL_3 | 全屏红色警告 + 强制确认 |
| 4 | REST_SUGGESTION | 休息建议弹窗 |

---

## 7. 责任矩阵

| 接口 | 板端 | 手机端 | 板端状态 |
|------|------|--------|---------|
| BLE 广播 + GATT Server | **实现** | — | ⏳ 待实现 |
| BLE 扫描 + 连接 + MTU | — | **实现** | — |
| `get_device_status` | **响应** | 轮询 | ⏳ 数据已就绪 |
| `get_current_state` | **响应** | 轮询 1-2s | ⏳ 数据已就绪 |
| `get_today_summary` | **响应** | 按需查询 | ⏳ 需 TodayStats 聚合器 |
| `get_last_report` | **响应** | 按需查询 | ⏳ 需 ReportGenerator 接入 |
| `session_control` | **响应** | 起停按钮 | ⏳ 需抽出独立接口 |
| `update_preferences` | **响应** | 模式切换 UI | ⏳ ModeManager 已就绪 |
| `reminder_ack` | **响应** | 消警按钮 | ⏳ onUserAck 已就绪 |
| `phone_sync` | **响应** | 前台 App 检测上报 | ⏳ 需 EventDetector 接入 |
| `push_state` | **推送** | 订阅 Notify | ⏳ 需变更监听 |
| `push_alert` | **推送** | 订阅 Notify | ⏳ 需事件回调 |
| `push_session_end` | **推送** | 订阅 Notify | ⏳ 需会话结束回调 |

---

## 8. 错误处理

### 板端错误响应

```json
{"status":"error","error":"<错误描述>"}
```

| 错误场景 | error 内容 |
|---------|-----------|
| JSON 解析失败 | `"invalid json"` |
| 未知 cmd | `"unknown command"` |
| data 缺失或格式错误 | `"invalid params: <字段名>"` |
| 业务逻辑错误 | 见各接口定义 |

### 手机端处理建议

- 收到 `status=error` → 展示 toast 提示，不崩溃
- BLE 断连 → 停止轮询，显示"连接断开"，自动重连
- 重连后重新订阅 RX Notify
- 开发板重启期间手机端独立运行不受影响

---

## 9. 手机端功能与 API 对应关系

手机端 readme 中列出的功能与本 API 的对应：

| 手机端功能 | 使用的 API | 板端依赖 |
|-----------|-----------|---------|
| NFC 控制面板模式 | `session_control` | Phase 5 |
| 家庭模式摄像头监控 | 不经过板端 BLE | 手机端 WiFi 直连（独立功能） |
| 前台 App 检测 | `phone_sync` | Phase 4.1 |
| 番茄钟离开前台通知 | 手机端本地实现 | 无 |
| 手机使用情况上报 | `phone_sync` | Phase 4.1 |
| 实时状态显示 | `get_current_state` + `push_state` | Phase 5 |
| 今日统计 | `get_today_summary` | Phase 3.1 |
| 会话报告 | `get_last_report` | Phase 1.2 |
| 远程起停 | `session_control` | Phase 5 |
| 模式切换 | `update_preferences` | 已就绪 |
| 消警 | `reminder_ack` | 已就绪 |

---

## 10. 变更记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v3.0 | 2026-06-15 | 初版契约，基于板端 v2.1 + 手机端反馈重写 |
| v3.1 | 2026-06-15 | `phone_sync` 新增 `distraction_level`、`app_name` 字段，信号走行为层状态机权重 |
| v3.2 | 2026-06-15 | `session_control` 新增 `target_duration_min`；`get_current_state` 新增 `target_duration_ms`、`remaining_ms`；新增 `get_console_data`、`push_timer_complete` |
| v3.3 | 2026-06-15 | `get_last_report` 新增 `longest_focus_streak_ms`、`phone_distraction_count`；`get_today_summary` 新增 `phone_distraction_count` |
| v3.4 | 2026-06-15 | FocusState 新增 `PHONE_IN_LEARNING`(8)、`PHONE_IN_PLAYING`(9)，手机干扰细分 |
