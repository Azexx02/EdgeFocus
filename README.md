# STM32G431 双轴云台副板

双轴连续旋转云台固件，使用双 AS5600 编码器、双 STSPIN830 驱动器和连续 FOC 位置保持。当前版本按“无机械限位、摄像头可侧装”的云台结构整理，P 轴已加入低增益平滑位置环和自适应重力补偿，实测已解决俯仰轴终点/克服重力时的持续抖动问题。

- PITCH 对应俯仰 P 轴，YAW 对应航向 Y 轴；协议不使用 X/XY 命名。
- 两轴逻辑角度均为 `0..359°`，不使用机械限位和撞限位标定。
- 串口控制为 UART4、115200、8N1、3.3 V TTL。
- 当前推荐纯串口模式：连接 TX、RX、GND，上电后发送 `ARM`，收到 `OK,ARM` 后才能运动，结束时发送 `STOP`。

文档：

- [文档导航与当前产品基线](docs/README.md)
- [接线、板级信号与 STM32MP257/ESP32 适配](docs/g431_board_control_signals.md)
- [串口命令速查表](docs/uart_command_table.md)

常用构建命令：

```powershell
cmake --preset Debug
cmake --build --preset Debug -j 4
```
