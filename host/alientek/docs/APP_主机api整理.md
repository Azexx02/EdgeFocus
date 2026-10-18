# APP_主机 API 整理

更新时间：2026-07-03

## 1. 总约定

App 通过 BLE GATT 写入 JSON 命令。主机响应 JSON，并通过同一 RX Notify 推送事件。

请求格式：

```json
{"cmd":"<command>","id":1,"data":{}}
```

响应格式：

```json
{"status":"success","id":1,"data":{}}
```

错误格式：

```json
{"status":"error","id":1,"error":"message"}
```

## 2. 枚举

### focus_state

| 值 | 名称 | App 含义 |
|---:|---|---|
| 0 | FOCUSED | 专注 |
| 1 | LOOKING_AWAY | 视线偏离 |
| 2 | SLEEPY_OR_SLUMPED | 疲劳/姿态异常 |
| 3 | PHONE_IN_USE | 手机使用输入 |
| 4 | AWAY | 离开 |
| 5 | LOW_CONFIDENCE | 视觉低置信 |
| 6 | PAUSED | 暂停 |
| 7 | CALIBRATING | 预检/校准 |
| 8 | PHONE_IN_LEARNING | 学习类手机使用输入 |
| 9 | PHONE_IN_PLAYING | 娱乐类手机使用输入 |

注意：`PHONE_IN_USE / PHONE_IN_PLAYING / PHONE_IN_LEARNING` 不是提醒等级。

### session_phase

`idle / precheck / focusing / paused`

### notification channel mask

| bit | 名称 |
|---:|---|
| 1 | SCREEN |
| 2 | BAND |
| 4 | SOUND |
| 8 | LIGHT，兼容输入，主机映射为 SCREEN |

### output route

`board / phone`

使用两个独立字段：

- `screen_output_route=board`：板载屏幕可用，主机可以使用板载屏幕做状态展示和屏幕提醒。
- `screen_output_route=phone`：板载屏幕整体不可用，由 App 完整承担状态展示、预检画面、报告展示和屏幕类提醒；主机不应依赖板载屏幕完成主流程。
- `sound_output_route=board`：板载声卡可用，主机可以播放提示音。
- `sound_output_route=phone`：板载声卡整体不可用，由 App 播放提示音；主机不应依赖板载声卡完成主流程。

这两个字段是硬件版本适配配置，不是普通用户提醒偏好。设为 `phone` 后，主机应旁路对应板载硬件，避免屏幕或声卡驱动异常卡住主流程。普通用户可配置的是 `normal_channels` / `silent_channels`。

## 3. 命令

### get_device_status

请求：

```json
{}
```

返回重点字段：

- `battery_available`
- `battery_percent`
- `power_source`
- `system_mode`
- `camera_ok`
- `bracelet_connected`
- `board_screen_available`
- `board_sound_available`
- `is_session_active`
- `output_capability`
- `gimbal_connected`
- `gimbal_online`
- `gimbal`

电池说明：当前主机还没有实现电池检测。未知时返回
`battery_available=false`、`battery_percent=null`、`battery_voltage_mv=null`；
App 不要把 null 显示成 0%。

说明：
- `output_capability` 是提醒输出能力与路由快照，包含 `board_screen_available`、`board_sound_available`、`screen_output_route`、`sound_output_route`、`band_available`、`last_attempted_channels(_mask)`、`last_delivered_channels(_mask)`。
- `board_screen_available` / `board_sound_available` 表示当前路由下板载输出是否会被使用且可用；当对应 route 为 `phone` 时它们会是 `false`，含义是主机主动旁路板载硬件，不等价于硬件损坏。
- `get_device_status.gimbal` / `get_gimbal_status` 属于诊断状态；它们可以在云台 precheck 未启用时仍返回状态对象，不能单独作为 App 进入云台校准页的依据。

### get_current_state

返回：

- `active`
- `paused`
- `session_id`
- `focus_state`
- `elapsed_ms`
- `remaining_ms`
- `target_duration_ms`
- `distraction_count`
- `reminder_count`
- `calibration_completed`
- `session_phase`
- `reminder`
- `precheck`
- `output_capability`
- `precheck_guidance`
- `camera_preview`
- `focus_reason`
- `behavior_context`

说明：
- `precheck` 总是包含 `camera`；只有主机确认本次会话真的可做云台预检时才包含 `gimbal`。
- `behavior_context` / `focus_reason` 是当前行为层上下文快照，便于状态页或调试页展示。

### session_control

请求：

```json
{"action":"start","target_duration_min":25}
```

动作：

- `start`
- `skip_precheck`
- `skip_calibration`
- `pause`
- `resume`
- `end`

`start` 可传：

- `target_duration_min`
- `target_duration_ms`
- `duration_ms`

未传时使用主机偏好 `default_focus_duration_min`。

`start` 成功后主机会返回当前会话状态。App 本来就需要等待该响应后再进入下一步 UI；新增/补充的关键字段是：

- `session_phase`：当前主机阶段，常见为 `precheck` 或 `focusing`。
- `precheck`：预检阶段结构化信息。
- `precheck.camera`：摄像头预检信息，包含预览和距离/姿态提示。
- `precheck.gimbal`：只有主机配置启用云台流程、运行时接入云台模块、且云台当前可执行运动控制时才返回。App 看到该对象才展示云台校准/控制区；没有该对象时，本次预检仍展示摄像头画面预览和距离/姿态指引，但不展示任何云台内容。

示例：

```json
{
  "success": true,
  "session_id": "session_...",
  "session_phase": "precheck",
  "precheck": {
    "session_phase": "precheck",
    "camera": {
      "required": true,
      "preview": {},
      "guidance": {}
    },
    "gimbal": {
      "enabled": true,
      "online": true,
      "safe": true,
      "angle_x": 90,
      "angle_y": 0
    }
  }
}
```

补充约束（以当前主机实现为准）：
- `start` 响应本身就决定 App 下一步渲染；App 继续等待这个响应是既有流程，不是新增流程。
- `start` / `get_current_state` 中的 `precheck.gimbal` 只有以下条件同时满足才返回：
  1. `gimbal_enabled=true`
  2. 主机进程未被 `EDGEFOCUS_DISABLE_GIMBAL=1` 禁用
  3. 主机运行时接入了云台控制模块
  4. 当前云台 `online=true` 且 `safe=true`，也就是运动控制可用
- 如果 `precheck` 中没有 `gimbal`，App 仍展示通用摄像头 precheck，包括画面预览、距离/姿态提示、继续/跳过；不要引导用户进入云台校准页，也不要把云台离线、不安全或故障错误暴露给普通用户。
- `start` 返回的关键字段至少有：`success`、`already_active`、`session_id`、`remaining_ms`、`target_duration_ms`、`focus_state`、`calibration_completed`、`session_phase`、`precheck`。

### update_preferences

请求字段可传任意子集：

```json
{
  "system_mode": 0,
  "away_timeout_min": 15,
  "default_focus_duration_min": 25,
  "vibration_strength": 0.5,
  "sound_volume": 0.7,
  "led_brightness": 75,
  "reminder_sensitivity": 0.5,
  "normal_channels_mask": 7,
  "silent_channels_mask": 3,
  "normal_channels": ["screen", "band", "sound"],
  "silent_channels": ["screen", "band"],
  "screen_output_route": "board",
  "sound_output_route": "board",
  "gimbal_enabled": true
}
```

约束：

- 未传字段保持原值；修改后写入主机偏好，后续启动继续生效。
- `default_focus_duration_min` clamp 到 5..120。
- `default_focus_duration_min` 是“默认会话时长”，只在 `session_control start` 未传本次时长时使用；App 首页本次手动调节应通过 `target_duration_min` 传入 `start`，不要写入默认值。
- `sound_volume` clamp 到 0..1。
- `normal_channels` / `normal_channels_mask` 是 normal 模式下用户提醒渠道偏好，可选 `SCREEN / BAND / SOUND`，默认全选。
- `silent_channels` / `silent_channels_mask` 是 silent 模式下用户提醒渠道偏好，只可选 `SCREEN / BAND`，默认全选；silent 不允许 SOUND，主机会过滤。
- 如果同时传 `*_channels_mask` 和 `*_channels`，当前主机实现优先使用 `*_channels_mask`。
- normal/silent 只控制渠道，不改变提醒等级。
- `screen_output_route=phone` 表示板载屏幕整体不可用，由 App 承担主展示和屏幕类提醒；主机应旁路板载屏幕。
- `sound_output_route=phone` 表示板载声卡整体不可用，由 App 承担提示音播放；主机应旁路板载声卡。
- `screen_output_route` / `sound_output_route` / `gimbal_enabled` 是工程/硬件版本适配配置，普通用户设置页不要做成随手切换项。
- `gimbal_enabled=false` 时，本次会话按无云台版本处理，主流程不返回 `precheck.gimbal`。
- `gimbal_enabled=false` 时，`precheck.gimbal` 不返回，`gimbal_control` 返回 disabled。
- `get_device_status.gimbal` / `get_gimbal_status` 仍可作为诊断状态查看，不等于主流程一定展示云台控制区。

优先级：

1. 主机内置默认值用于首次启动或偏好缺失。
2. `update_preferences` 写入的用户/工程偏好覆盖内置默认值。
3. 环境变量 `EDGEFOCUS_DISABLE_LCD / EDGEFOCUS_DISABLE_SOUND / EDGEFOCUS_DISABLE_GIMBAL` 是硬件兜底开关，可在启动时覆盖对应偏好。
4. `session_control start.target_duration_min` 是本次会话临时值，只影响当前 session，不覆盖 `default_focus_duration_min`。

### get_preferences

返回当前偏好字段，包括：

- `system_mode`
- `away_timeout_min`
- `default_focus_duration_min`
- `vibration_strength`
- `sound_volume`
- `led_brightness`
- `reminder_sensitivity`
- `normal_channels_mask`
- `silent_channels_mask`
- `normal_channels`
- `silent_channels`
- `screen_output_route`
- `sound_output_route`
- `gimbal_enabled`

### phone_sync

请求：

```json
{
  "is_in_use": true,
  "distraction_level": 2,
  "phone_state": "playing",
  "app_name": "example",
  "app_category": "entertainment"
}
```

兼容字段：

- `is_in_use` 或 `active`
- `phone_state` / `usage_state` / `state` / `app_category`
- `distraction_level` 为 1..3

返回：

- `focus_state`
- `action_taken`
- active 时包含 `distraction_count`、`reminder_count`

`is_in_use=false` 会清除主机侧旧手机压力，不再继续按旧手机状态提醒。
注意：
- `phone_sync` 响应只是“主机已接收/清除这次手机输入”的回执，不是最终行为判定结果。
- `focus_state`、`distraction_count`、`reminder_count` 只代表响应时刻的当前快照；真正的状态推进与提醒结果应以后续 `get_current_state` / `push_state` / `push_alert` 为准。

### get_camera_preview_image

返回最近一次主机保存的摄像头预览图：

- `available`
- `timestamp_ms`
- `sequence`
- `width`
- `height`
- `pixel_format`
- `encoding`
- `source`
- `data_base64`
- `quality`

App 可按较低频率拉取，用作“拍照式预览”，不要假装实时视频。

### get_gimbal_status

返回：

- `connected`
- `online`
- `safe`
- `xhome`
- `yhome`
- `calibrated`
- `boost`
- `motor`
- `en`
- `ix`
- `iy`
- `angle_x`
- `angle_y`
- `last_seen_ms`
- `last_error`
- `initializing`
- `auto_initialized`
- `last_command`
- `last_command_status`
- `last_command_response`
- `enabled`

### gimbal_control

请求：

```json
{"action":"move_xy","x_deg":3,"y_deg":0,"speed":10}
```

动作：

- `stop`
- `center_x`
- `deg_x`
- `deg_y`
- `set_xy`
- `move_xy`

参数：

- `x_deg`: 0..180
- `y_deg`: 0..359
- `speed`: 1..90

App 用户手动控制建议小步调整，默认步长 3 度，最大 8 度。

当 `gimbal_enabled=false` 时，接口返回错误，App 应禁用云台控制按钮。

补充约定：
- App 普通用户 precheck 流程优先使用 `nudge_left / nudge_right / nudge_up / nudge_down / move_xy / set_xy / stop`。
- 运动类动作要求主机侧云台 `online=true` 且 `safe=true`；未满足时主机返回错误，App 应在云台页内提示等待、重试或跳过云台预检，不要退出整个会话。
- `auto_init / center_x / deg_x / deg_y / ver / ping / stat / fault / angle` 如保留，应明确标为主机调试/诊断入口，不应作为普通用户主流程按钮。

诊断类云台动作 `ver / ping / stat / fault / angle` 需要额外传 `debug=true`；
普通 App 用户流程不要调用这些动作。

### get_today_summary

返回：

- `total_seconds`
- `focus_seconds`
- `sessions`
- `events`
- `phone_distraction_count`

### get_last_report

返回最近完成会话：

- `session_id`
- `total_duration`
- `focus_duration`
- `distraction_count`
- `reminder_count`
- `phone_distraction_count`
- `feedback`

无报告时 `data=null`。

### get_session_history

请求：

```json
{"cursor":"","limit":10}
```

返回：

- `sessions`
- `next_cursor`
- `has_more`

### session_feedback

请求：

```json
{"session_id":"...","feedback":"just_right"}
```

feedback 取值：

- `too_much`
- `too_little`
- `just_right`

### reminder_ack

请求：

```json
{}
```

返回：

- `cooling_ms`

### get_console_data

Debug/development only. Production App flow should use `get_current_state`,
`get_device_status`, report/history APIs, preferences, and push events. Do not
build ordinary user UI that depends on `behavior_context` internals from this
command.

调试/展示用综合状态，包含：

- `session`
- `device`
- `reminder`
- `output_capability`
- `frame_quality`
- `precheck_guidance`
- `camera_preview`
- `behavior_context`
- `focus_reason`
- `today`

## 4. Push 事件

### push_state

触发：状态变化、暂停/恢复、跳过预检等。

### push_alert

触发：策略层产生提醒。

重点字段：

- `level`
- `message`
- `risk_score`
- `phone_distraction_level`
- `vision_signal`
- `phone_signal`
- `reason`
- `phone_actions`
- `sound_volume`
- `focus_state`
- `reminder_count`

App 如果看到 `phone_actions` 包含 `SCREEN` 或 `SOUND`，需要承担对应展示或提示音。
`phone_actions` 是主机已经决定交给手机承接的提醒渠道列表，不是 App 自己再做渠道决策。
`sound_volume` 是当 `phone_actions` 包含 `SOUND` 时建议 App 播放提示音的音量值（0..1），不是提醒等级。
`output_capability.last_attempted_channels` 包含主机本次选中的板载和手机承接渠道；
`last_delivered_channels` 中，手机承接渠道在 `push_alert` 发出后计为 delivered，
板载渠道只在设备路径报告执行成功后计为 delivered。

### push_session_end

触发：会话结束。

### push_timer_complete

触发：倒计时完成。
