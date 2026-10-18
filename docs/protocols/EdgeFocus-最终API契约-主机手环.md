# EdgeFocus 最终 API 契约 - 主机与手环

> 日期：2026-06-30  
> 来源：`edgefocus_total/api文档/BLE_Signal_Protocol.md`、主机 `BleBandController`、手环 `ble_protocol` / `ble_band` 当前代码交叉校对。  
> 性质：字段级契约。手环已送封装，后续默认不主动改固件。

---

## 1. BLE 链路

| 项目 | 值 |
|---|---|
| 主机角色 | BLE Central |
| 手环角色 | BLE Peripheral |
| 手环广播名 | `EdgefocusBand` |
| Service UUID | `0x180C` / `0000180c-0000-1000-8000-00805f9b34fb` |
| Characteristic UUID | `0x2A56` / `00002a56-0000-1000-8000-00805f9b34fb` |
| Characteristic 权限 | Write |
| 默认 MTU | 23，有效载荷约 20 字节 |
| BLE 模块 | E104-BT52，UART 透明传输 |
| 手环 MCU | STM32G030 |
| 当前业务方向 | 主机写入手环 |
| 当前无上行 | 无业务 ACK、无 Notify、无状态读取、无手环电量上报 |

App 不连接手环。App 不能直接指定手环震动。手环只接收主机最终策略后的帧。

---

## 2. E104 配置

手环当前配置命令口径：

```text
AT+ROLE=0
AT+ADV=1
AT+TRANMD=1
AT+ONSLEEP=0
AT+NAME=EdgefocusBand
UUIDSVR raw 0C18
UUIDMAST raw 562A
```

LINK 脚用于判断连接状态。断连后手环应回到 advertising 状态。

---

## 3. 帧格式

通用格式：

```text
Byte0 = type
Byte1 = param1
Byte2 = param2
Byte3-N = optional payload
```

当前手环解析类型：

| Byte0 | 类型 | 当前状态 |
|---|---|---|
| `0x01` | Alert | 主机运行时当前使用 |
| `0x02` | TimeSync | 手环固件支持，主机主流程未接入 |
| `0x03` | Text | 手环固件支持，主机主流程未接入 |
| 单字节任意值 | Legacy Alert | 手环按 legacy alert 处理 |
| 其他多字节 | Legacy Alert fallback | 当前代码 fallback 到 legacy alert，不是忽略 |

---

## 4. Alert 帧

主机发送：

```text
Byte0 = 0x01
Byte1 = vibration_pattern
Byte2 = level
```

示例：

```text
01 03 03
```

含义：快速短震，等级 3。

### 4.1 pattern

| 值 | 名称 | 手环行为 |
|---|---|---|
| `0x00` | NONE | 不震动，仅可更新显示 |
| `0x01` | SLOW | 3 次短震，间隔约 500ms |
| `0x02` | LONG | 长震，level >= 3 时约 1000ms，否则约 800ms |
| `0x03` | FAST | 3 次短震，间隔约 100ms |
| `0xFF` | LEGACY | legacy 短震，约 500ms |

短震 pulse 当前由固件按至少约 300ms 处理。

### 4.2 level

| 值 | 名称 |
|---|---|
| `1` | LEVEL_1 |
| `2` | LEVEL_2 |
| `3` | LEVEL_3 |
| `4` | REST_SUGGESTION |

重要：level 是主机策略层最终输出，不由 App 或 phone_sync 直接决定。

### 4.3 主机 pattern 映射

主机 `BleBandController::sendAlert(level)` 当前映射：

```text
level >= 3 -> pattern 0x03
level >= 2 -> pattern 0x02
else       -> pattern 0x01
```

主机写入的 Alert frame 固定为 3 字节：

```text
{0x01, pattern, level}
```

---

## 5. TimeSync 帧

手环支持：

```text
Byte0 = 0x02
Byte1 = minutes_hi
Byte2 = minutes_lo
```

计算：

```text
remaining_minutes = (minutes_hi << 8) | minutes_lo
```

示例：

```text
02 00 1E  -> 30 分钟
02 00 00  -> 倒计时结束或停止
```

当前状态：手环固件支持倒计时显示，但主机 C++ 主流程当前未周期发送 TimeSync。最终主链路不依赖该帧。

---

## 6. Text 帧

手环支持：

```text
Byte0 = 0x03
Byte1-N = text bytes
```

当前口径：

- 固件可把 Byte1-N 交给文本显示 callback。
- 当前可靠范围优先按 ASCII 理解。
- 不承诺 UTF-8 中文 Text 可靠显示。
- 主机 C++ 主流程当前不发送 Text。

---

## 7. 主机 BlueZ 写入流程

主机运行时使用 C++ BlueZ system D-Bus，不以 shell 脚本作为主链路。

```text
BleBandController
  -> set adapter powered
  -> discovery / match EDGEFOCUS_BAND_MAC or EdgefocusBand
  -> Device1.Connect
  -> wait ServicesResolved
  -> find 0000180c and 00002a56
  -> GattCharacteristic1.WriteValue([0x01, pattern, level])
```

环境变量：

| 变量 | 说明 |
|---|---|
| `EDGEFOCUS_BAND_NAME` | 默认 `EdgefocusBand` |
| `EDGEFOCUS_BAND_MAC` | 可指定目标手环 MAC |
| `EDGEFOCUS_BAND_ENABLED` | 控制手环链路启用 |

---

## 8. 诊断字段

主机可通过 App `get_console_data.device` 暴露手环诊断：

| 字段 | 类型 | 说明 |
|---|---|---|
| `bracelet_connected` | bool | 主机侧认为是否连接 |
| `band_last_alert_level` | int | 最近一次发送等级 |
| `band_last_alert_pattern` | int | 最近一次 pattern |
| `band_write_attempt_count` | int | 写入尝试次数 |
| `band_last_write_status` | string | 最近写入结果或错误 |

这些字段只能说明主机写入路径，不等价于手环业务已执行完成。手环没有 ACK。

---

## 9. 当前实测和冻结口径

继承事实：

- 手环 OLED 中文 UI 已完成并实测通过。
- 充电休眠、按键熄屏、长按重配蓝牙已实测通过。
- 写入 `010101` 有响应。
- 手环已送封装，默认不主动改固件。

必须补测：

| 场景 | 验收 |
|---|---|
| 主机真实写 `01 03 03` | 手环震动/OLED 提示 |
| 手环断连/重连 | 恢复后主机可再次写 Alert |

---

## 10. 禁止误用

不得在文档或代码中假设：

- 手环有业务 ACK。
- 手环有 Notify。
- 主机能读取手环电量。
- App 能直连手环。
- App 或 phone_sync 能直接指定 `pattern`。
- Text 中文显示是可靠主链路能力。

如未来需要 ACK/Notify/电量上报，必须新增协议并走 `P0-接口变更流程.md`。
