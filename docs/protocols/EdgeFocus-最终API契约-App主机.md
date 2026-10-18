# EdgeFocus 最终 API 契约 - App 与主机

> 日期：2026-06-30  
> 来源：`edgefocus_total/api文档` 旧 API 文件、App 当前代码、主机当前代码、P0 协议冻结清单交叉校对。  
> 性质：字段级契约。后续 App 与主机联调优先读本文。旧 `api文档/` 目录只作为原始参考，不作为权威。

---

## 1. BLE GATT 通道

| 项目 | 值 |
|---|---|
| 主机角色 | BLE Peripheral / GATT Server |
| App 角色 | BLE Central |
| 主机广播名 | `EdgeFocus` |
| Service UUID | `0000FF00-0000-1000-8000-00805F9B34FB` |
| Write Characteristic | `0000FF01-0000-1000-8000-00805F9B34FB`，App 写入 JSON |
| Read/Notify Characteristic | `0000FF02-0000-1000-8000-00805F9B34FB`，主机响应和 push |
| CCCD | `00002902-0000-1000-8000-00805F9B34FB` |
| App 请求 MTU | 517 |
| 当前分片 | 无应用层分片，一条 Notify/Read 必须是一条完整 UTF-8 JSON |

App 首次扫描按名称 `EdgeFocus`，开发兼容 `ESP32`，以及 FF00 Service 识别。绑定后按保存 MAC 重连。

---

## 2. 消息格式

### 2.1 普通请求

App 发送：

```json
{"cmd":"get_current_state","id":12,"data":{}}
```

| 字段 | 类型 | 必须 | 说明 |
|---|---|---|---|
| `cmd` | string | 是 | 命令名 |
| `id` | int | 是 | App 底层自动补，主机必须原样回显 |
| `data` | object | 否 | 命令参数；无参数时可省略或为空对象 |

### 2.2 普通响应

主机返回：

```json
{"status":"success","id":12,"data":{}}
```

错误：

```json
{"status":"error","id":12,"error":"Invalid command data: ..."}
```

规则：

- 普通响应必须包含 `status` 和同一个 `id`。
- App 只把带匹配 `id` 的响应交给挂起请求。
- `status=error` 时 App 视为命令失败。
- JSON 解析失败时主机当前返回 `id=0`。

### 2.3 主动推送

主机推送：

```json
{"cmd":"push_state","data":{}}
```

规则：

- push 使用 `cmd=push_*`。
- push 不作为普通响应匹配。
- 当前 App 已消费 `push_state`、`push_session_end`、`push_timer_complete`。
- `push_alert` 主机已发送，App 当前没有业务消费。

---

## 3. 命令清单

最终普通命令为 12 个：

```text
get_device_status
get_current_state
get_today_summary
get_last_report
session_control
update_preferences
reminder_ack
phone_sync
get_console_data
session_feedback
get_session_history
get_preferences
```

Push 为 4 个：

```text
push_state
push_alert
push_session_end
push_timer_complete
```

---

## 4. 枚举

### 4.1 FocusState

| 值 | 名称 | 说明 |
|---|---|---|
| 0 | `FOCUSED` | 专注 |
| 1 | `LOOKING_AWAY` | 视线偏离 |
| 2 | `SLEEPY_OR_SLUMPED` | 疲劳或趴桌 |
| 3 | `PHONE_IN_USE` | 手机使用输入参与后的状态 |
| 4 | `AWAY` | 离座 |
| 5 | `LOW_CONFIDENCE` | 视觉低置信 |
| 6 | `PAUSED` | 暂停 |
| 7 | `CALIBRATING` | 预检/云台页 |
| 8 | `PHONE_IN_LEARNING` | 学习类手机使用 |
| 9 | `PHONE_IN_PLAYING` | 娱乐/强干扰类手机使用 |

重要规则：`PHONE_IN_USE` 和 `PHONE_IN_PLAYING` 不是固定提醒等级，只是行为输入/状态。最终提醒等级由主机策略层决定。

### 4.2 SessionPhase

| 值 | 条件 | 说明 |
|---|---|---|
| `idle` | `active=false` | 无会话 |
| `precheck` | `active=true && paused=false && calibration_completed=false` | 预检/云台页，用户手动 skip 后进入专注 |
| `focusing` | `active=true && paused=false && calibration_completed=true` | 正式专注 |
| `paused` | `active=true && paused=true` | 暂停 |

### 4.3 SystemMode

| 值 | 名称 | 说明 |
|---|---|---|
| 0 | `NORMAL` | normal channels 可用 |
| 1 | `SILENT` | silent channels 可用 |
| 2 | `HOME` | 旧枚举保留；当前 normal/silent 与工作策略不要混写 |

`SILENT` 不降低严重程度，只筛选无声提醒渠道。

### 4.4 CommandLevel

| 值 | 名称 |
|---|---|
| 0 | `NONE` |
| 1 | `LEVEL_1` |
| 2 | `LEVEL_2` |
| 3 | `LEVEL_3` |
| 4 | `REST_SUGGESTION` |

---

## 5. `get_device_status`

请求：

```json
{"cmd":"get_device_status","id":1}
```

响应 `data`：

| 字段 | 类型 | 说明 |
|---|---|---|
| `battery_percent` | int | 主机电量或未知值 |
| `system_mode` | int | `SystemMode` |
| `camera_ok` | bool | 摄像头状态 |
| `bracelet_connected` | bool | 主机与手环是否连接 |
| `is_session_active` | bool | 当前是否有活动会话 |

---

## 6. `get_current_state`

请求：

```json
{"cmd":"get_current_state","id":2}
```

响应 `data`：

| 字段 | 类型 | 说明 |
|---|---|---|
| `active` | bool | 是否有活动会话 |
| `paused` | bool | 是否暂停 |
| `session_id` | string | 当前会话 ID；无会话可为空 |
| `focus_state` | int | `FocusState` |
| `elapsed_ms` | int64 | 已进行毫秒数 |
| `remaining_ms` | int64 | 剩余毫秒数 |
| `target_duration_ms` | int64 | 目标毫秒数 |
| `distraction_count` | int | 当前会话分心数 |
| `reminder_count` | int | 当前会话提醒数 |
| `calibration_completed` | bool | 是否完成预检/云台 skip |
| `session_phase` | string | `idle/precheck/focusing/paused` |

App 应以 `active/session_phase/calibration_completed/paused` 判断页面，而不是只看 `focus_state`。

---

## 7. `session_control`

请求：

```json
{"cmd":"session_control","id":3,"data":{"action":"start","target_duration_min":25}}
```

`action`：

| 值 | 说明 |
|---|---|
| `start` | 启动会话 |
| `current` | 旧文档提及；当前主机代码未实现该 action，App 应用 `get_current_state` |
| `pause` | 暂停 |
| `resume` | 恢复 |
| `end` | 结束；App 代码里的 `stop` 会映射为 `end` |

start 可用时长字段：

| 字段 | 单位 | 优先级 |
|---|---|---|
| `target_duration_min` | 分钟 | 最高，App 当前使用 |
| `target_duration_ms` | 毫秒 | 兼容 |
| `duration_ms` | 毫秒 | 兼容 |

未传有效时长时，主机使用 `preferences.default_focus_duration_min`。

### 7.1 start 响应 `data`

| 字段 | 类型 | 说明 |
|---|---|---|
| `success` | bool | 是否成功 |
| `already_active` | bool | 主机已有会话时为 true，不重复创建 |
| `session_id` | string | 会话 ID |
| `remaining_ms` | int64 | 剩余时间 |
| `target_duration_ms` | int64 | 目标时间 |
| `focus_state` | int | 当前状态，通常为 `CALIBRATING` |
| `calibration_completed` | bool | start 后通常 false |
| `session_phase` | string | start 后通常 `precheck` |

### 7.2 pause/resume 响应 `data`

返回完整 session state，并额外包含：

| 字段 | 类型 | 说明 |
|---|---|---|
| `success` | bool | 是否成功 |

pause/resume 成功后主机会主动推送 `push_state`。

### 7.3 end 响应 `data`

| 字段 | 类型 | 说明 |
|---|---|---|
| `success` | bool | 是否成功 |
| `session_id` | string | 结束的会话 ID |
| `total_duration` | int | 总时长，秒 |
| `focus_duration` | int | 专注时长，秒 |
| `distraction_count` | int | 分心数 |
| `reminder_count` | int | 提醒数 |
| `phone_distraction_count` | int | 手机干扰数 |
| `remaining_ms` | int64 | 结束后为 0 |
| `target_duration_ms` | int64 | 原目标时长 |

end 成功后主机会 push `push_session_end`。

---

## 8. `phone_sync`

请求：

```json
{
  "cmd":"phone_sync",
  "id":4,
  "data":{
    "is_in_use":true,
    "app_category":"entertainment",
    "distraction_level":2,
    "app_name":"com.example.video",
    "duration_sec":120
  }
}
```

请求 `data`：

| 字段 | 类型 | 必须 | 说明 |
|---|---|---|---|
| `is_in_use` | bool | 是 | 手机是否正在使用 |
| `app_category` | string | 否 | `learning/social/entertainment/other` 等 |
| `distraction_level` | int | 否 | 手机侧输入强度；主机归一化使用，不等于最终等级 |
| `app_name` | string | 否 | 包名；桌面/后台可用 `desktop_or_background` |
| `duration_sec` | int | 否 | 当前前台持续秒数 |

响应 `data`：

| 字段 | 类型 | 说明 |
|---|---|---|
| `focus_state` | int | 当前快照状态；可能是处理前快照 |
| `action_taken` | bool | 是否写入/处理 phone signal |
| `distraction_count` | int | 当前会话分心数，active true 时有意义 |
| `reminder_count` | int | 当前会话提醒数，active true 时有意义 |

规则：

- App 不应依赖 `phone_sync` 响应立即切 UI。
- 最终状态以 `push_state` 或后续 `get_current_state` 为准。
- `app_name="local_session_sync"` 是 App 本地模式同步 today focus 的兼容路径，不属于主链路。

---

## 9. `get_today_summary`

响应 `data`：

| 字段 | 类型 |
|---|---|
| `total_seconds` | int64 |
| `focus_seconds` | int64 |
| `sessions` | int |
| `events` | int |
| `phone_distraction_count` | int |

---

## 10. `get_last_report`

无报告时：

```json
{"status":"success","id":5,"data":null}
```

有报告时 `data`：

| 字段 | 类型 |
|---|---|
| `session_id` | string |
| `total_duration` | int |
| `focus_duration` | int |
| `distraction_count` | int |
| `reminder_count` | int |
| `phone_distraction_count` | int |
| `feedback` | string |

---

## 11. `get_session_history`

请求：

```json
{"cmd":"get_session_history","id":6,"data":{"cursor":"opaque_cursor","limit":1}}
```

请求 `data`：

| 字段 | 类型 | 说明 |
|---|---|---|
| `cursor` | string | 可省略；上一页返回的 `next_cursor` |
| `limit` | int | 当前联调建议 1；App 代码允许 1..100，但无分片阶段应保持小包 |

响应 `data`：

| 字段 | 类型 | 说明 |
|---|---|---|
| `sessions` | array | 会话数组 |
| `next_cursor` | string/null | 下一页游标 |
| `has_more` | bool | 是否还有更多 |

`sessions[]` 字段：

| 字段 | 类型 |
|---|---|
| `session_id` | string |
| `start_time_ms` | int64 |
| `end_time_ms` | int64 |
| `total_duration` | int |
| `focus_duration` | int |
| `distraction_count` | int |
| `reminder_count` | int |
| `phone_distraction_count` | int |
| `feedback` | string |

---

## 12. `session_feedback`

请求：

```json
{"cmd":"session_feedback","id":7,"data":{"session_id":"session_...","feedback":"just_right"}}
```

`feedback` 只允许：

```text
too_much
too_little
just_right
```

成功响应：

```json
{"status":"success","id":7,"data":{"success":true}}
```

错误场景：

| 场景 | 错误 |
|---|---|
| 缺 `session_id` | `Missing session_id` |
| 非法 feedback | `Invalid feedback value: ...` |
| 找不到 session | `Session not found: ...` |
| 保存失败 | `Failed to save feedback` |

---

## 13. `get_preferences` 与 `update_preferences`

`get_preferences` 响应 `data`：

| 字段 | 类型 | 说明 |
|---|---|---|
| `system_mode` | int | 0 normal, 1 silent |
| `away_timeout_min` | int | 离座超时，不是默认专注时间 |
| `default_focus_duration_min` | int | 首页默认专注时间，默认 25 |
| `vibration_strength` | float | 震动强度配置 |
| `led_brightness` | int | 灯光亮度配置 |
| `reminder_sensitivity` | float | 提醒灵敏度 |

`update_preferences` 请求为增量更新：

```json
{"cmd":"update_preferences","id":8,"data":{"default_focus_duration_min":25}}
```

可更新字段同上。类型必须正确，错误类型会返回 `status=error`。

范围口径：

- `default_focus_duration_min` 主机 clamp 到 5..120。
- `away_timeout_min` 单位分钟。
- `system_mode` 只影响提醒渠道选择，不改变提醒严重程度。

---

## 14. `reminder_ack`

请求：

```json
{"cmd":"reminder_ack","id":9}
```

响应 `data`：

| 字段 | 类型 | 当前值 |
|---|---|---|
| `cooling_ms` | int64 | 15000 |

---

## 15. `get_console_data`

用于诊断，不作为普通 UI 主数据源。

响应 `data` 包含：

| 对象 | 关键字段 |
|---|---|
| `session` | `active/paused/session_id/focus_state/elapsed_ms/target_duration_ms/remaining_ms/distraction_count/reminder_count/calibration_completed/session_phase` |
| `device` | `battery_percent/system_mode/camera_ok/bracelet_connected/band_last_alert_level/band_last_alert_pattern/band_write_attempt_count/band_last_write_status` |
| `reminder` | `level/message/since_ms` |
| `frame_quality` | `score/reason/guidance_needed` |
| `today` | `total_seconds/focus_seconds/sessions/events/phone_distraction_count` |

---

## 16. Push 事件

### 16.1 `push_state`

主机当前来源：phone_sync 消费、视觉状态变化、pause/resume 等。

`data`：

| 字段 | 类型 | 说明 |
|---|---|---|
| `focus_state` | int | 当前 FocusState |
| `elapsed_ms` | int64 | 已进行 |
| `distraction_count` | int | 分心数 |
| `session_id` | string | 会话 ID |
| `calibration_completed` | bool | 是否完成预检 |
| `active` | bool | pause/resume 路径包含 |
| `paused` | bool | pause/resume 路径包含 |
| `remaining_ms` | int64 | pause/resume 路径包含 |
| `target_duration_ms` | int64 | pause/resume 路径包含 |
| `reminder_count` | int | pause/resume 路径包含 |
| `session_phase` | string | pause/resume 路径包含 |

App 当前只用 `focus_state` 映射 timer state，后续可按完整字段增强。

### 16.2 `push_timer_complete`

`data`：

| 字段 | 类型 |
|---|---|
| `elapsed_ms` | int64 |
| `target_duration_ms` | int64 |
| `distraction_count` | int |
| `reminder_count` | int |
| `phone_distraction_count` | int |
| `session_id` | string |

App 当前 `TimerComplete` 只消费前四个字段，后两个字段可作为增强。

### 16.3 `push_session_end`

`data`：

| 字段 | 类型 |
|---|---|
| `session_id` | string |
| `total_duration` | int |
| `focus_duration` | int |
| `distraction_count` | int |
| `reminder_count` | int |
| `phone_distraction_count` | int |

### 16.4 `push_alert`

主机已发送，App 当前未消费。

`data`：

| 字段 | 类型 |
|---|---|
| `level` | int |
| `message` | string |
| `risk_score` | int |
| `phone_distraction_level` | int |
| `vision_signal` | bool |
| `phone_signal` | bool |
| `reason` | string |
| `since_ms` | int64 |
| `reminder_count` | int |

`push_alert` 不应排在主链路前面，但字段保留用于诊断和后续 UI。

---

## 17. 当前实现差异和联调注意

| 项 | 契约口径 |
|---|---|
| `Mobile_API_Contract_v2.md` | 已旧，不作为最终依据 |
| `Mobile_API_Contract_v2NEW.md` / `board-communication.md` | 旧参考，部分口径已过时或乱码，仅作为字段来源 |
| `get_session_history.limit` | App 可发 1..100，当前联调建议 1，避免无分片超包 |
| `push_alert` | 主机已发，App 未业务消费 |
| `phone_sync.focus_state` 响应 | ACK/调试，不作为 UI 最终状态 |
| `current` action | 旧文档提过，当前主机未实现，使用 `get_current_state` |
| `default_focus_duration_min` | 当前新增权威字段，区别于 `away_timeout_min` |
