# BLE Signal Protocol

适用对象：EdgefocusBand 手环端与 STM32MP2 主设备之间的 BLE GATT 写入协议。  
当前手环工程：`stm32/STM32Band_test/`。

## GATT

| 项目 | 值 |
|---|---|
| BLE 角色 | 手环为 peripheral，主设备为 central |
| 广播名 | `EdgefocusBand` |
| Service UUID | `0x180C` |
| Characteristic UUID | `0x2A56` |
| 数据方向 | Host -> Band write |

说明：部分 BLE 调试工具会把 `0x180C` 显示为 `0x0C18`，这是字节序显示差异。

## Frame

```text
Byte 0: type
Byte 1-N: payload
```

| Type | 名称 | 说明 |
|---:|---|---|
| `0x01` | Alert | 振动/提醒 |
| `0x02` | TimeSync | 倒计时分钟数 |
| `0x03` | Text | OLED 临时文本 |

## Alert

```text
01 <pattern> [level]
```

| pattern | 含义 |
|---:|---|
| `0x00` | none |
| `0x01` | slow 3x |
| `0x02` | long |
| `0x03` | fast 3x |

`level` 可省略，省略时手环按 `1` 处理。

| level | 手环显示建议/当前实现 |
|---:|---|
| 1 | 轻提醒 |
| 2 | 注意 |
| 3 | 专注提醒 |
| 4+ | 休息提醒 |

示例：

```text
01 01 01    Alert, slow 3x, level 1
01 03 04    Alert, fast 3x, level 4
01 02       Alert, long, default level 1
```

## TimeSync

```text
02 <minutes_hi> <minutes_lo>
```

分钟数为 big-endian：

```c
minutes = (minutes_hi << 8) | minutes_lo;
```

示例：

```text
02 00 1E    30 分钟倒计时
02 00 00    退出倒计时，回到主页面
```

## Text

```text
03 <utf8_text...>
```

手环显示前 21 字节文本，约 1.5 秒后回到倒计时页或主页面。

示例：

```text
03 48 65 6C 6C 6F    显示 "Hello"
```

## Compatibility

为了兼容旧版主设备和 BLE 调试工具写入：

| 输入 | 当前手环行为 |
|---|---|
| 空写入 | 忽略 |
| 1 字节写入 | legacy Alert |
| 未知 type | legacy Alert |
| malformed frame | legacy Alert |

legacy Alert 只振动一次，不显示 OLED。当前振动时长为 500 ms。
