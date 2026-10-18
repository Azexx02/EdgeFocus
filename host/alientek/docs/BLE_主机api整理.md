# BLE_主机 API 整理

更新时间：2026-07-03

本文整理主机相关 BLE 接口，分为 App 到主机 BLE GATT，以及主机到手环 BLE GATT。

## 1. App ↔ 主机 BLE GATT

角色：

- 主机：BLE Peripheral / GATT Server
- App：BLE Central / GATT Client

Service 与 Characteristic：

| 项 | UUID | 方向 | 说明 |
|---|---|---|---|
| Service | `0000FF00-0000-1000-8000-00805F9B34FB` | - | EdgeFocus App 主服务 |
| TX | `0000FF01-0000-1000-8000-00805F9B34FB` | App 写主机 | JSON 命令写入 |
| RX | `0000FF02-0000-1000-8000-00805F9B34FB` | App 读/订阅主机 | JSON 响应与 Push |

传输格式：

```json
{
  "cmd": "get_current_state",
  "id": 1,
  "data": {}
}
```

成功响应：

```json
{
  "status": "success",
  "id": 1,
  "data": {}
}
```

失败响应：

```json
{
  "status": "error",
  "id": 1,
  "error": "message"
}
```

Push 事件：

```json
{
  "cmd": "push_state",
  "data": {}
}
```

App 接入要求：

- 连接后先订阅 RX Notify，再写 TX。
- 命令响应和 Push 都走 RX。
- App 不直接连接手环。
- App 不重算提醒等级，提醒等级以主机返回为准。

## 2. 主机 ↔ 手环 BLE GATT

角色：

- 主机：BLE Central
- 手环：BLE Peripheral

协议：

| 项 | UUID | 方向 | 说明 |
|---|---|---|---|
| Service | `0000180C-0000-1000-8000-00805F9B34FB` | - | EdgefocusBand 服务 |
| Characteristic | `00002A56-0000-1000-8000-00805F9B34FB` | 主机写手环 | Alert 二进制帧 |

Alert 帧：

```text
byte[0] = 0x01
byte[1] = vibration_pattern
byte[2] = level
```

当前主机映射：

| CommandLevel | pattern | level |
|---|---:|---:|
| LEVEL_1 | `0x01` | `1` |
| LEVEL_2 | `0x02` | `2` |
| LEVEL_3 | `0x03` | `3` |

主机写入路径：

```text
ReminderPolicy
-> NotificationRouter
-> DeviceManager.issueAction(BAND)
-> BleBandController.sendAlert(level)
-> GATT WriteValue 0x2A56
```

诊断字段：

- `band_last_alert_level`
- `band_last_alert_pattern`
- `band_write_attempt_count`
- `band_last_write_status`
- `bracelet_connected`

## 3. 当前限制

- 手环无 ACK。
- 手环无 Notify。
- 手环无电量上报。
- 手环无状态读取。
- 手环充电时休眠，取下并连接后接收主机 Alert。

这些限制不影响当前主链路演示，因为主机只需要写 Alert 并在状态中记录写入尝试。

