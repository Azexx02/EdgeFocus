# App 云台人工调整字段（主机协议 3.0）

## 请求

命令仍为 `gimbal_nudge`。App 不发送角度，只为当前这一次点击增加可选布尔字段：

```json
{
  "cmd": "gimbal_nudge",
  "id": 101,
  "data": {
    "session_id": "session-id",
    "owner_id": "app-instance-id",
    "lease_token": "lease-token",
    "sequence": 42,
    "direction": "RIGHT",
    "fine_tune": true
  }
}
```

- `fine_tune` 缺省或 `false`：普通调整，步进由主机配置，当前为 30°。
- `fine_tune=true`：本次微调，步进由主机配置，当前为 10°。
- 它不是模式开关；普通按钮和微调按钮可以任意交替点击。
- `direction` 仍为 `UP / DOWN / LEFT / RIGHT`。
- App 不发送 `step_deg`、绝对角度或 ±70°边界值。

## 成功响应新增字段

```json
{
  "accepted": true,
  "fine_tune": true,
  "resolved_step_deg": 10.0,
  "motion_status": "firmware_accepted",
  "boundary_stop_planned": false,
  "boundary_paused": false,
  "axes": {
    "yaw": {"decision": "manual_full_range"},
    "pitch": {"decision": "manual_full_range"}
  }
}
```

`resolved_step_deg` 仅用于界面提示和日志，App 不应据此自行累计目标角度。

## ±70°单次停靠

从 ±70°范围内向外跨越时，主机会先把本次目标截到边界：

```json
{
  "accepted": true,
  "motion_status": "moving_to_boundary",
  "boundary_stop_planned": true,
  "boundary_paused": true
}
```

物理尚未稳定时继续同方向点击，响应为：

```json
{
  "accepted": false,
  "error": "gimbal_boundary_settle_pending",
  "motion_status": "boundary_pending",
  "boundary_paused": true
}
```

App 此时应短暂禁用同方向的普通/微调两个按钮，并显示“正在边界停靠”；其他方向不需要锁死。主机确认稳定后，下一次同方向点击即可越过边界，且普通或微调按钮都能消费这次许可。设备本来已稳定在 ±70°时，第一次同方向点击直接运动。

## 兼容与错误处理

- 老 App 不传 `fine_tune` 时按普通 30°处理。
- `fine_tune` 不是布尔值时主机拒绝请求。
- 收到 `OUTCOME_UNKNOWN`、链路错误或 Fault 时不要自动重发 nudge；重新获取主机状态和租约后再由用户操作。
- 所有 nudge 仍需递增 `sequence`，并携带有效人工控制租约。
