# G431 云台串口命令速查表

本文只用于现场查命令。完整流程和返回格式见 [UART 控制协议](gimbal_uart_control_protocol.md)。

## 串口与坐标

- G431 UART4：`115200 8N1`，3.3 V TTL，无硬件流控。
- 命令不区分大小写，以 `\n` 或 `\r\n` 结束。
- `P` = Pitch（俯仰），`Y` = Yaw（航向）。协议不再使用 X/XY 命名。
- 当前推荐纯串口模式；上电后必须先发送 `ARM`，收到 `OK,ARM` 后才能运动。

## 启停与运动

| 命令 | 参数 | 作用 |
|---|---:|---|
| `ARM` | — | 执行两轴电角度对齐并进入可运动状态 |
| `STOP` / `DISARM` | — | 停止轨迹，关闭保持、PWM 和 Boost，并清除 ARM 状态 |
| `!` | 单字节 | 无需等待换行的紧急停止 |
| `HOLD 1` | — | 捕获当前位置并开启保持 |
| `HOLD 0` | — | 关闭位置保持 |
| `PITCH n` / `DEG P n` | `0..359` | 设置 P 轴目标角度 |
| `YAW n` / `DEG Y n` | `0..359` | 设置 Y 轴目标角度 |
| `SET YP yaw pitch` | 各 `0..359` | 同时更新 Y/P 两轴目标 |
| `TRACK YP yaw10 pitch10` | 各 `0..3599` | 0.1° 单位的跟踪目标，推荐 20–50 ms 更新 |
| `CIRCLE radius period` | `1..60°`、`1000..60000 ms` | 以当前姿态为起点运行一圈 |

运动命令是异步目标更新，不会阻塞串口。未执行 `ARM` 时，运动命令返回 `ERR,NOT_ARMED,USE_ARM`。

## 查询与维护

| 命令 | 作用 |
|---|---|
| `PING` | 通信检查，正常返回 `OK,G431` |
| `VER` | 固件、协议版本和能力 |
| `STAT` | 模式、功率、保持、目标和故障状态 |
| `ANGLE` | 编码器原始值和 P/Y 逻辑角度 |
| `FAULT` | 故障、电流和 UART 计数 |
| `FOCSTAT` | 对齐、位置误差、FOC 输出、重力补偿和看门狗 |
| `GRAVCLEAR` | 清除 P 轴本次运行的重力补偿学习值 |
| `HELP` / `HELP ALL` | 命令帮助 |
| `ECHO text` | 回显通信测试 |

## 推荐现场测试

```text
PING
VER
STAT
ANGLE
ARM
PITCH 10
YAW 10
ANGLE
STOP
STAT
FAULT
```

通过标准：出现 `BOOT_ALIGN,OK` 和 `OK,ARM`；P/Y 两轴分别响应；最终 `BOOST=0,MOTOR=0,HOLD=0,FAULT=0`。
