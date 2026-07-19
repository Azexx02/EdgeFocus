# STM32G431 云台 UART 控制协议

## 1. 适用范围

本文定义 G431 云台应用固件协议 2.0。产品集成推荐使用纯串口模式：只连接 TX、RX、GND，通过 `ARM`、运动命令和 `STOP` 完成控制。板级硬件联锁是可选增强，见 [板间控制信号](g431_board_control_signals.md)。

## 2. 物理接口

| 项目 | 参数 |
|---|---|
| G431 外设 | UART4 |
| G431 TX / RX | PC10 / PC11 |
| 格式 | 115200、8N1、无流控 |
| 电平 | 3.3 V TTL，禁止直接接 5 V |
| 帧结束 | `\n` 或 `\r\n` |

TX/RX 必须交叉连接，两块板必须共地。

## 3. 坐标约定

- `P`：Pitch，俯仰轴。
- `Y`：Yaw，航向轴。
- 两轴使用 `0..359°` 环形坐标，并选择最短方向运动。
- `SET YP yaw pitch` 和 `TRACK YP yaw10 pitch10` 的参数顺序固定为先 Y、后 P。
- 协议 2.0 不支持 `X`、`XY`、`DEG X` 或 `SET XY`。

## 4. 状态机与标准流程

```text
上电/STOP
   │
   ├─ PING、VER、STAT、ANGLE 可查询
   │
   └─ ARM
       ├─ BOOT_ALIGN,START
       ├─ BOOT_ALIGN,OK
       └─ OK,ARM       ← 收到这里才允许运动
              │
              ├─ PITCH / YAW / SET YP / TRACK YP / CIRCLE
              └─ STOP  → OK,STOP，返回安全未使能状态
```

主控不得用固定延时替代 `OK,ARM` 判断。对齐失败时记录 `ANGLE`、`STAT`、`FAULT` 的完整返回，并保持停止状态。

## 5. 常用控制

```text
ARM
PITCH 30
YAW 120
SET YP 120 30
HOLD 1
TRACK YP 1200 300
CIRCLE 10 12000
STOP
```

`TRACK YP` 使用 0.1° 单位，适合视觉程序每 20–50 ms 更新。`CIRCLE 10 12000` 表示半径 10°、周期 12 秒的一圈轨迹。

## 6. 返回与诊断

- 成功响应通常以 `OK,` 开头；异步过程会先返回 `...,START`，完成后返回 `...,OK`。
- `ANGLE` 的 `P_RAW/Y_RAW` 为编码器原始值，`P_DEG/Y_DEG` 为逻辑角度。
- `STAT` 中 `BOOST/MOTOR/HOLD` 表示功率和保持状态。
- `FOCSTAT` 中 `PE/YE` 为 P/Y 误差，`PDUTY/YDUTY` 为输出，`PGRAV` 为 P 轴重力补偿。
- `UARTERR/RXDROP/TXDROP/WD` 不应持续增加。偶发历史计数应结合增量判断。

## 7. 安全要求

- 任何失控迹象立即发送单字节 `!`，并准备切断电机电源。
- 编码器、驱动或控制看门狗异常会关闭 PWM 和 Boost。
- 两轴没有软件机械限位；连续旋转必须使用滑环、结构限位或主控累计角度保护。
- 更换相机、线束、配重或安装方向后执行 `GRAVCLEAR`，再在常用角度保持数秒。
- 断电、复位或重新 `ARM` 后，本次启动的零点和重力学习状态可能变化；上层不能把它当作绝对机械零点。
