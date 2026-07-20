# G431 协议 3.0 命令速查

## 协商与只读诊断

```text
PING
VER
HELLO HOST PROTO=3.0
STAT
FAULT
ANGLE
FOCSTAT
NVM STATUS
POSE
```

只有 `PING/VER/STAT/FAULT/ANGLE/FOCSTAT/NVM STATUS` 可在 HELLO 前使用。输入命令大小写敏感，最大 255 字节。

## 生产运动

| 命令 | 允许状态 | 结果 |
|---|---|---|
| `ARM` | DISABLED | 校准/原点/电流保护均有效时进入 READY；不执行校准 |
| `HOLD NOW` | READY/HOLDING/MOVING | 原子捕获当前位置，返回新 generation |
| `TRACK YP yaw10 pitch10` | HOLDING/MOVING | 接受目标并返回 `TRACK,ACCEPTED,GEN=n` |
| `POSE` | 任意 | 返回当前/目标角、generation、settled、epoch、故障和维护进度 |
| `STOP` | 使能/校准/FAULT | 确保 PWM/Boost 关闭；不清 NVM |
| `!` | 任意 | 无需换行的独立紧急停止 |

`yaw10/pitch10` 为 `0..3599` 的 0.1° 环形坐标。目标还会由永久原点 ±70° 安全窗校验。ACK 不是到位；只接受同 generation 的 `POSE,SETTLED=1`。

## 维护命令

```text
CAL ELECTRICAL P|Y|ALL
ORIGIN CAPTURE
ARM MAINTENANCE
CURRENT RESET
CURRENT
PROTECT CURRENT SET P_PEAK=<raw> P_AVG=<raw> Y_PEAK=<raw> Y_AVG=<raw> STALL_MS=<ms>
PROTECT CURRENT CLEAR
NVM CLEAR ORIGIN|ALL
FAULT CLEAR
```

- `CAL ELECTRICAL` 在 DISABLED 下异步运行，先返回 `CAL,ACCEPTED,OP_GEN=n`；通过 `POSE.OP/OP_PROGRESS/LAST_OP_STATUS` 跟踪完成。
- `ORIGIN CAPTURE` 在人工摆正且编码器稳定时保存永久机械正前方。
- `ARM MAINTENANCE` 只允许永久原点 ±10°、低 PWM、低速验证。
- `CURRENT` 返回两轴 raw/1000 ms average/reset 后 peak、窗口、样本数、VALID 与 SATURATED。
- NVM 清除是 A/B 事务 tombstone，必须由上层做独立二次确认。

## 错误

```text
ERR,CLASS=COMMAND,CODE=...
ERR,CLASS=TRANSIENT,CODE=...
ERR,CLASS=FAULT,CODE=...
```

COMMAND 拒绝不等于硬件故障；FAULT 立即停止运动并锁存，物理条件恢复、输出关闭后才能 `FAULT CLEAR`。主机不得高速循环 ARM/CLEAR。
