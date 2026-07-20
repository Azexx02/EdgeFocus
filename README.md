# STM32G431 双轴云台副板（协议 3.0）

本分支是 EdgeFocus 国赛版云台固件。G431 负责两轴实时控制、安全停机、持久化电气参数/机械原点和电流保护；STM32MP257 主机负责 PRECHECK 搜索、会话跟踪和跨端控制。

- UART4：115200、8N1、3.3 V TTL，命令必须以 `\r\n` 或 `\n` 结束。
- 主机必须先完成 `VER` 和 `HELLO HOST PROTO=3.0` 协商。
- 普通 `ARM` 不校准、不改变零位；缺少电气校准、机械原点或电流保护时拒绝生产使能。
- `TRACK YP yaw10 pitch10` 使用 0.1° 环形坐标，ACK 只表示目标已接受；物理到位以同 generation 的 `POSE,SETTLED=1` 为准。
- 单字节 `!` 是独立急停入口；`STOP` 关闭 PWM/Boost，但不清除 NVM。
- 电气校准、永久原点、电流保护和 NVM 清除只允许通过主机维护向导执行。

构建：

```powershell
cmake --preset Debug
cmake --build --preset Debug -j 4
cmake --preset Release
cmake --build --preset Release -j 4
```

文档：

- [协议 3.0 命令速查](docs/uart_command_table.md)
- [板级信号与接线](docs/g431_board_control_signals.md)
- 主机完整规格：`focus_app_national/docs/superpowers/specs/2026-07-20-gimbal-protocol-v3-national-redesign.md`
