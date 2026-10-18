# EdgeFocus App Final Integration Contract

This document is the authoritative App-facing contract for the current host-first architecture.

## Core Semantics

- `PHONE_IN_USE`, `PHONE_IN_LEARNING`, and `PHONE_IN_PLAYING` are **phone usage input labels only**.
- They are not reminder levels and are not final behavior outputs.
- Behavior/risk context is built from:
  - vision facts (events/state from `VisionResult` and camera pipeline)
  - phone inputs from `phone_sync`
  - history and timers in the behavior/policy state
- `ReminderPolicy` computes `CommandLevel` only from behavior/risk context.
- Output command level is `LEVEL_1 ~ LEVEL_3`.
- `SILENT` mode does not reduce severity; it only filters channels.
- `NORMAL`/`HOME` mode determine which reminder channels are allowed for execution.

## BLE API Contract

Service: `0000FF00-0000-1000-8000-00805F9B34FB`  
Write (`TX`): `0000FF01-0000-1000-8000-00805F9B34FB`  
Read/Notify (`RX`): `0000FF02-0000-1000-8000-00805F9B34FB`

Request/response envelope:

```json
{"cmd":"<command>","id":1,"data":{}}
```

Success response:

```json
{"status":"success","id":1,"data":{}}
```

Error response:

```json
{"status":"error","id":1,"error":"..."}
```

## App Flow (Verified)

`start` -> `skip_precheck` -> `focusing` -> repeated `phone_sync` (optional) ->
vision/phone behavior update -> `ReminderPolicy` emits `LEVEL_1~3` -> reminders ->
`pause`/`resume` -> `end` -> report/history/feedback.

Missing `phone_sync` must not block the session.

## Commands

### `get_device_status`

Returns device and availability state:

- `battery_percent`, `battery_voltage_mv`, `battery_charging`, `power_source`
- `system_mode`
- `camera_ok`
- `bracelet_connected`, `board_screen_available`, `board_sound_available`
- `is_session_active`
- `output_capability`
- `gimbal_connected`, `gimbal_online`, `gimbal`
- `last_attempted_channels_mask`, `last_delivered_channels_mask`
- `last_attempted_channels`, `last_delivered_channels`

### `get_current_state`

Returns snapshot of current behavior/session state:

- `active`, `paused`, `session_id`, `focus_state`, `elapsed_ms`,
  `remaining_ms`, `target_duration_ms`
- `distraction_count`, `reminder_count`
- `calibration_completed`, `session_phase`
- `reminder`, `output_capability`, `precheck_guidance`,
  `camera_preview`, `focus_reason`, `behavior_context`

### `session_control`

Data fields:

- `action`: `start`, `skip_precheck`, `skip_calibration`, `pause`, `resume`, `end`
- `start` accepts `target_duration_min` (or `target_duration_ms`/`duration_ms` fallback)

### `phone_sync`

Expected request payload (example):

```json
{"cmd":"phone_sync","id":1,"data":{"is_in_use":true,"distraction_level":2,"app_category":"social","app_name":"example","duration_sec":10}}
```

Response policy:

- `focus_state`: always returned
- `action_taken`: always returned
- `distraction_count`, `reminder_count`: only when the active code path returns them

### `update_preferences` / `get_preferences`

Preference fields:

- `system_mode`, `away_timeout_min`, `default_focus_duration_min`
- `vibration_strength`, `sound_volume`, `led_brightness`, `reminder_sensitivity`
- `normal_channels_mask`, `silent_channels_mask` (or `normal_channels`, `silent_channels`)
- `screen_output_route`, `sound_output_route`

Mode rules:

- `normal` channels default: `SCREEN/BAND/SOUND`
- `silent` channels default: `SCREEN/BAND` (no `SOUND`)

### `reminder_ack`

Payload: `{}`  
Response: `{ "cooling_ms": <int> }`

### `get_last_report` / `get_session_history`

- `get_last_report`: latest report object, or `data:null` if none exists
- `get_session_history`: paginated report list with `next_cursor`/`has_more`

### `session_feedback`

Writes feedback for prior report (`just_right`, `too_much`, `too_little`).

## Push Events

- `push_state`: focus and session state updates
- `push_alert`: reminder event
- `push_session_end`: end-of-session report payload
- `push_timer_complete`: timer completion payload

### `push_alert`

Reminder payload includes:

- `level` (`1..3`)
- `message`, `risk_score`, `phone_distraction_level`
- `vision_signal`, `phone_signal`, `reason`
- `phone_actions`: channels requested by policy after mode/channel filtering (e.g. `SCREEN`, `SOUND`, `BAND`)
- `sound_volume`
- `since_ms`, `focus_state`, `reminder_count`

Only channels available to current mode/policy may be present in `phone_actions`.
