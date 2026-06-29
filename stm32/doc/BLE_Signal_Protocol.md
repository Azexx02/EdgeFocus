# ESP32-Band 蓝牙信号解析协议

## 概述

主机（STM32MP2）通过 BLE GATT 写入特征值 `0x2A56` 向手环发送指令。手环固件需解析写入的字节数组，根据内容执行对应动作。

## BLE 配置

| 项目 | 值 |
|------|-----|
| 设备广播名 | `EdgefocusBand` |
| Service UUID | `0x180C` |
| Characteristic UUID | `0x2A56` |
| 权限 | Write |
| MTU | 默认 23 字节（有效载荷 20 字节） |

## 数据帧格式

```
+--------+--------+--------+----------+
| Byte 0 | Byte 1 | Byte 2 | Byte 3-N |
| 类型   | 参数1  | 参数2  | 可选载荷 |
+--------+--------+--------+----------+
```

### Byte 0: 消息类型

| 值 | 类型 | 说明 |
|----|------|------|
| `0x01` | Alert | 提醒/振动指令 |
| `0x02` | TimeSync | 剩余时间同步 |
| `0x03` | Text | 文本显示（预留） |
| 其他 | - | 忽略 |

### 向前兼容

旧版主机可能只发送 1 字节（任意值）。固件应兼容：收到 1 字节数据时，按 `Alert` 类型处理，默认短振。

---

## 类型 0x01: Alert（提醒）

触发振动，用于注意力分散提醒。

```
Byte 0: 0x01
Byte 1: 振动模式 (VibrationPattern)
Byte 2: 提醒级别 (Level)
```

### 振动模式 (Byte 1)

| 值 | 模式 | 描述 |
|----|------|------|
| `0x00` | 无振动 | 仅更新屏幕，不振动 |
| `0x01` | 连续缓慢短震 | 三次短振间隔 500ms（温和提醒） |
| `0x02` | 长振 | 一次持续振动（约 800ms） |
| `0x03` | 连续快速短振 | 三次短振间隔 100ms（紧急） |

### 提醒级别 (Byte 2)

| 值 | 级别 | 含义 |
|----|------|------|
| `1` | LEVEL_1 | 轻微提醒（视线偏移） |
| `2` | LEVEL_2 | 中等提醒（玩手机） |
| `3` | LEVEL_3 | 严重提醒（趴桌/离位） |
| `4` | REST_SUGGESTION | 建议休息 |

### OLED 显示建议

收到 Alert 时，OLED 可显示：
- 级别 1-2：振动 + 图标提示
- 级别 3-4：振动 + 红色警告 + 提醒文字

### 解析伪代码

```c
void handle_alert(uint8_t vibration, uint8_t level) {
    switch (vibration) {
        case 0x00: /* no vibration */ break;
        case 0x01:                                 // slow pattern
            for (int i = 0; i < 3; i++) {
                motor_vibrate(300);
                delay(500);
            }
            break;
        case 0x02: motor_vibrate(800); break;      // long buzz
        case 0x03:                                 // pattern
            for (int i = 0; i < 3; i++) {
                motor_vibrate(300);
                delay(100);
            }
            break;
    }
    oled_show_alert(level);
}
```

---

## 类型 0x02: TimeSync（时间同步）

向手环同步当前番茄钟剩余时间，用于倒计时显示。

```
Byte 0: 0x02
Byte 1: 分钟数高字节 (big-endian)
Byte 2: 分钟数低字节 (big-endian)
```

### 计算

```
remaining_minutes = (Byte 1 << 8) | Byte 2
```

### 示例

| 发送数据 | 含义 |
|----------|------|
| `{0x02, 0x00, 0x1E}` | 剩余 30 分钟 |
| `{0x02, 0x00, 0x0F}` | 剩余 15 分钟 |
| `{0x02, 0x00, 0x00}` | 时间到 |

### OLED 显示建议

收到 TimeSync 时，OLED 可显示倒计时 `MM:SS`。每分钟主机发送一次更新。

### 解析伪代码

```c
void handle_time_sync(uint8_t hi, uint8_t lo) {
    uint16_t minutes = (hi << 8) | lo;
    oled_show_countdown(minutes);
}
```

---

## 类型 0x03: Text（文本显示，预留）

向手环发送文本内容显示在 OLED 上。

```
Byte 0:   0x03
Byte 1-N: UTF-8 编码文本
```

### 示例

| 发送数据 | 含义 |
|----------|------|
| `{0x03, 0xE4, 0xBC, 0x91, 0xE6, 0x81, 0xAF}` | "休息" |
| `{0x03, 0x4F, 0x4B}` | "OK" |

### 解析伪代码

```c
void handle_text(const uint8_t* data, size_t len) {
    char text[64];
    size_t copy_len = len < 63 ? len : 63;
    memcpy(text, data + 1, copy_len);  // skip byte 0
    text[copy_len] = '\0';
    oled_show_text(text);
}
```

---

## 完整解析入口

```c
void on_ble_write(const uint8_t* data, size_t len) {
    if (len == 0) return;

    // 向前兼容：1 字节 = 旧版 Alert
    if (len == 1) {
        handle_alert(0x01, 1);  // 默认短振 + 级别1
        return;
    }

    uint8_t type = data[0];

    switch (type) {
        case 0x01:  // Alert
            if (len >= 3) {
                handle_alert(data[1], data[2]);
            } else if (len >= 2) {
                handle_alert(data[1], 1);
            }
            break;

        case 0x02:  // TimeSync
            if (len >= 3) {
                handle_time_sync(data[1], data[2]);
            }
            break;

        case 0x03:  // Text
            handle_text(data, len);
            break;

        default:
            // 未知类型，忽略
            break;
    }
}
```

---

## 时序说明

```
主机                              手环
 |                                 |
 |--- Connect ------------------>|  OLED: "BLE Connected"
 |                                 |
 |--- Alert {01,01,02} --------->|  短振 + 显示提醒
 |                                 |
 |--- TimeSync {02,00,1E} ------>|  显示 "30:00"
 |                                 |
 |--- Alert {01,03,03} --------->|  连续振动 + 红色警告
 |                                 |
 |--- Disconnect --------------->|  OLED: "BLE Advertising"
```

## 注意事项

- 写入操作无需响应（Write Without Response 也可）
- 单次写入最大 20 字节（默认 MTU）
- 手环收到数据后应立即执行，无需确认
- 连接断开后手环应自动恢复广播状态
