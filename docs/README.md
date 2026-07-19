# G431 云台文档导航

建议按以下顺序阅读：

1. [云台副板固件说明](云台副板程序说明.md)：了解功能、P/Y 坐标、控制策略和验收方法。
2. [接线、板级信号与主控适配](g431_board_control_signals.md)：选择纯串口或硬件联锁模式，并查看 MP257/ESP32 接线。
3. [UART 控制协议](gimbal_uart_control_protocol.md)：实现主控状态机、返回解析和安全处理。
4. [串口命令速查表](uart_command_table.md)：调试现场快速查命令。
5. [ESP32-S3 测试主机](examples/esp32_s3_gimbal_host.md)：使用 Arduino 框架做串口透传和测试。

## 当前产品基线

- 协议：2.0，只使用 P/Y 命名。
- 串口：115200、8N1、3.3 V TTL。
- 推荐模式：纯串口，TX + RX + GND。
- 启动：`ARM`，等待 `BOOT_ALIGN,OK` 和 `OK,ARM`。
- 停止：`STOP` 或单字节 `!`。
- STM32MP257：UART4 PB6/PB7 AF4，Linux `/dev/ttySTM3`，已完成实机测试。

## 文档维护规则

- 命令定义只在协议和速查表维护，其他文档通过链接引用。
- 对外只写 P/Y，不再新增 X/XY 兼容说明。
- 示例必须默认安全上电、先 ARM、最后 STOP。
- 修改固件协议时同步更新版本、命令表和验收流程。

