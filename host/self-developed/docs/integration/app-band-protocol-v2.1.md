# EdgeFocus 正式会话 App / 手环协议 v2.1

本文只描述正式会话的跨设备交互。板端是会话、视觉事实、手机事实融合与分神等级的唯一业务决策端。

## 1. App 连接时序

1. App 连接板端 GATT 并订阅 RX notification。
2. 板端立即发送一次 `push_state`，App 用它覆盖本地缓存状态。
3. App 发送 `client_hello`：

```json
{
  "cmd": "client_hello",
  "id": 1,
  "data": {
    "capabilities": ["event_ack_v1", "event_dedupe_v1", "phone_heartbeat_v1"]
  }
}
```

4. 板端返回 `schema_version=2.1`、`reliable_push`、`phone_fresh_for_ms=5000`，以及
   `external_power_v1`、`power_profile_override_v1` 等 `server_capabilities`。
5. 重连后必须重新订阅、重新发送 `client_hello`，再以新的 `push_state` 为准。支持可靠推送时，尚未确认且未过期的关键事件会自动重放。

## 2. 正式会话控制

正式会话只能使用板端现场摄像头，App 不上传或选择视觉源。

```json
{
  "cmd": "session_control",
  "id": 10,
  "data": {
    "action": "start",
    "target_duration_min": 25,
    "source_mode": "live_camera",
    "operation_id": "android-1784000000000-1"
  }
}
```

主机收到合法 `start` 后先同步发布 `PRECHECK + preparation_state=INITIALIZING` 并立即响应，摄像头、推理 runtime、演示资源和 PRECHECK feature 的实际准备在单一后台 worker 中完成。App 收到该响应即可打开预检页，但在准备态变成 `READY` 或 `DEGRADED` 前不得发送继续、云台或预览命令。

`runtime_snapshot.session` 额外返回：

- `preparation_state`：`NONE | INITIALIZING | READY | DEGRADED | FAILED`；
- `operation_id`：原样回显本次 App 启动事务 ID；
- `start_generation`：主机启动代次；
- `preparation_error`、`preparation_failed_at_ms`：失败原因与墙钟时间。

`start` 只发送一次。若直接响应丢失或超时，App 在协议连接重新达到 READY 后，以每秒一次 `get_current_state`、最多 30 秒核对 `operation_id` 和主机权威阶段；不得因本地剩余时间或首次响应缺少阶段而推断已经 FOCUSING。重复 `start` 只返回当前会话，不建立第二个 worker。

允许的后续动作保持为 `skip_precheck`、`pause`、`resume`、`end`。`runtime_snapshot.session.runtime_active=true` 表示 PRECHECK runtime 已建立或正在建立，但此时 `runtime_snapshot.session.active=false`；只有确认进入 FOCUSING 后 `active=true`，正式计时、SQLite 和分神记录才开始。事件 ACK 必须携带当前 `session_id`。

## 3. 手机原始事实

App 只上报“是否使用手机”和前台类别，不上报分神等级。类别为：

- `learning`：弱分神/抵消证据；
- `playing`：娱乐类强证据；
- `using`：其他或未知使用，中等证据；
- 未使用：`is_in_use=false`。

PRECHECK 不接受 `phone_sync`。板端返回 `accepted=false` 与 `error_code=formal_session_not_started`；App 应在确认进入 FOCUSING 后立即发送一次当前手机状态，再保持 2 秒心跳。

会话内 `sequence` 必须严格单调递增。App 在状态不变时也应每 2 秒发送一次新 sequence 的心跳；板端固定按 5 秒有效期处理。完全相同的重复包可幂等接受，但不会刷新有效期；同 sequence 不同内容和乱序包会被拒绝。

```json
{
  "cmd": "phone_sync",
  "id": 20,
  "data": {
    "session_id": "session_...",
    "sequence": 42,
    "timestamp_ms": 1784000000000,
    "is_in_use": true,
    "usage_state": "playing"
  }
}
```

App 应读取响应里的 `accepted`、`last_accepted_sequence` 与 `fresh_for_ms`。若板端返回拒绝，App 应从下一 sequence 继续发送当前真实状态，不要复用冲突 sequence。

## 4. 关键事件确认

需要可靠处理的板端事件为 `push_alert` 和 `push_session_end`。App 以 `event_id` 去重，并按实际处理进度确认：

- `received`：完整分片已重组、JSON 已解析并持久化到 App 本地队列；
- `presented`：提醒已真正展示或声音已真正开始播放；
- `user_ack`：用户主动确认提醒。

```json
{
  "cmd": "event_ack",
  "id": 30,
  "data": {
    "event_id": "evt_...",
    "session_id": "session_...",
    "stage": "presented"
  }
}
```

确认阶段只能前进。重复或较低阶段返回 `duplicate`，App 应视为成功；`session_mismatch`、`unknown_event` 和 `expired` 不得改写为成功。

只有 `push_alert.data.phone_actions` 指定的输出由 App 执行。目前板载屏幕和板载声音已移除，因此 App 需要实现：

- `SCREEN`：显示提醒卡片；
- `SOUND`：按 `sound_volume` 播放提示音；
- 对同一 `event_id` 只执行一次输出，即使收到重放。

本轮板端与 App 作为破坏性协议升级同步发布，不再为旧 App 返回已经删除的电量或供电别名。

## 5. 分片重组

超过单包上限的响应使用 `status=chunk`，推送使用 `cmd=push_chunk`。App 必须按同一消息的 `seq/total` 收齐并按序拼接，对 `data` 做 `base64_utf8` 解码后再解析原 JSON；只有完整重组后才能发送 `received` ACK。

App 重组上限为：最多 4 个待组消息、每消息 8192 片、解码后 768 KiB、单片 128 字节、`chunk_id` 128 字符；元数据冲突或同序号内容冲突时丢弃整个消息。摄像头预览当前为 `GRAY8 + base64`（40×30），不是 JPEG；App 必须在后台解码并对 Base64 长度、解码字节数与图像尺寸做上限检查。

## 6. 手环输出

手环仍由板端作为 BLE Central 直接控制，App 不转发手环提醒。

当前板载适配器未声明 BlueZ `central-peripheral` 并发角色，实测 App 与主机连接期间手环无法同时建立连接；App 断开后手环可正常连接并解析下列服务。因此现硬件版本以 App 通道为优先，手环自动连接默认关闭，`band_control` 只能请求已启用的手环服务，不能通过重启 Bluetooth 或牺牲 App 连接强制成功。需要 App/手环并发的后续硬件必须增加第二 BLE 控制器或采用无需第二条连接的协议。

- Service UUID：`0000180c-0000-1000-8000-00805f9b34fb`
- Characteristic UUID：`00002a56-0000-1000-8000-00805f9b34fb`
- 写入帧：`[0x01, pattern, level]`
- level/pattern：1/`0x01`、2/`0x02`、3/`0x03`

板端发送采用“1 个正在写入 + 1 个待发送”的非阻塞队列：高等级覆盖低等级待发项，断线不消耗失败次数，重连后在 10 秒 TTL 内补发；已连接但写失败按 500 ms、1500 ms 间隔重试，单次 GATT 写最长阻塞 1 秒。`last_delivered_channels_mask` 只在实际写成功后标记手环通道。

## 7. 外部供电与 Home/Go

板端只上报“是否检测到外部供电”，不再提供电量百分比、电池电压或充电状态。以下字段出现在
`get_device_status`、`get_current_state` 和状态推送中：

```json
{
  "external_power_state": "PRESENT",
  "external_power_present": true,
  "external_power_sample_age_ms": 120,
  "power_profile": "home",
  "power_profile_source": "auto_external",
  "manual_override_active": false
}
```

- `external_power_state`：`PRESENT | ABSENT | UNKNOWN`；
- `external_power_present`：已确认时为布尔值，未知时为 `null`；
- `external_power_sample_age_ms`：从最近一次成功读取起算，尚无成功样本时为 `null`；
- `power_profile_source`：`auto_external | auto_no_external | auto_failsafe | manual`；
- 自动策略：外部供电为 Home，无外部供电或检测未知为 Go；
- App 手动覆盖只在当前供电 epoch 有效，下一次确认的插拔会自动清除，不跨板端重启保存。

手动选择使用独立运行时命令，不能写入 `update_preferences`：

```json
{
  "cmd": "set_power_profile",
  "id": 40,
  "data": {"power_profile": "go"}
}
```

必须删除旧字段：`battery_available`、`battery_percent`、`battery_voltage_mv`、
`battery_charging`、`power_source`。App 不得用默认 67%、本地充电推断或旧字段回退补齐 UI。

Home/Go 只调整板端应用视觉任务频率；蓝牙、提醒送达、风扇安全闭环和 G431 电机实时环不降频。

## 8. 当前云台能力边界

云台是否出现由板端实测配置与当次 UART/PD6/对齐健康门决定。即使状态缓存中出现旧 `gimbal_online=true`，App 也不得显示
控制入口。只有 `client_hello.data.server_capabilities` 明确包含 `gimbal_precheck_v1` 时，App 才能在
当前会话 PRECHECK 阶段使用以下命令：

```text
precheck_skip_auto_adjust
gimbal_nudge(direction = UP | DOWN | LEFT | RIGHT)
gimbal_center
gimbal_stop
gimbal_realign
precheck_confirm_position
```

`session_control.skip_precheck` 与 PRECHECK 阶段的 `session_control.end` 应携带当前 `session_id`。板端在执行状态转换前核对当前 runtime；不匹配时返回 `success=false, message=session_id_mismatch`，避免重连或旧响应把命令作用到新会话。未携带该字段的旧客户端暂时保持兼容。

每条云台命令的 `data` 必须带当前 `session_id` 和严格递增的无符号 `sequence`。板端只接受固定 small
nudge，App 不上传步长或原始角度。自动搜索结束后才授予 `manual_lease`；确认位置后 App 再发送正常
会话 `session_control.skip_precheck` 进入 FOCUSING。`runtime_snapshot.gimbal` 返回搜索状态、租约、
waypoint、角度、recoverable 和故障锁存原因。任一运行条件失败时，PRECHECK 运动和会话内跟踪能力同时降级；App 只展示状态，不自行重试运动。板端低频探测恢复并在重新对齐成功后更新快照。

不得提供原始角度、UART 文本、BOOT0 或复位入口。能力缺失、session 不匹配、非 PRECHECK、旧
sequence 和无手动租约都必须作为拒绝处理，不能在 App 本地伪装成功。

## 9. App 最小改动清单

- 建立 `event_id` 去重表，至少保留 2 分钟；
- notification 订阅成功后立即发送 `client_hello`；
- 对关键事件实现 `received/presented/user_ack`；
- 正式会话期间每 2 秒发送手机事实心跳；
- 断线时停止假设手机事实仍有效，重连后以 `push_state` 重建状态；
- 不在 App 内再次计算分神等级；
- 删除所有电量和“正在充电”组件，按第 7 节接入外部供电与 Home/Go；
- Home/Go 手动切换只发送 `set_power_profile`，不写本地长期偏好；
- 不向 App 暴露或恢复云台控制入口，直到能力协商明确开放。
