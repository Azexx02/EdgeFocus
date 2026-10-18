# 蓝牙手环固件

EdgeFocus 的低打扰提醒终端。主机通过 BLE GATT（Service `0x180C`，Characteristic `0x2A56`）写入提醒帧，手环执行振动与 OLED 显示。

## 两代硬件

| 目录 | 硬件 | 工具链 | 状态 |
| --- | --- | --- | --- |
| `stm32/STM32Band_test/` | STM32G030C8T6 + E104-BT52 | CMake + Ninja + arm-none-eabi | **主线固件**，OLED / 马达 / 电池 / 电源 / 按键 / BLE 已集成 |
| `stm32/Drivers/` | 可复用 STM32 模块 | C / HAL | 模块真源 |
| `esp32/` | ESP32-S3 开发板 | ESP-IDF + NimBLE | 旧硬件参考 |

## BLE 协议

```text
Byte 0: type
  0x01 = Alert       Byte1 = pattern, Byte2 = level
  0x02 = TimeSync    Byte1..2 = 大端分钟数
  0x03 = Text        Byte1..N = UTF-8 文本
```

- 单字节写入按旧版 Alert 处理（短振，level 1）。
- Alert pattern：`0x00` 无、`0x01` 慢振 3 次、`0x02` 长振、`0x03` 快振 3 次。
- 当前手环无 ACK、Notify、状态读取与电量上报；主机按 TTL 与连接状态做降级。

## STM32G030 引脚

| 功能 | 引脚 | 配置 |
| --- | --- | --- |
| OLED SDA / SCL | PB7 / PB6 | I2C1，400 kHz |
| 马达 | PA6 | TIM16_CH1 PWM，5 kHz |
| BLE UART TX / RX | PA9 / PA10 | USART1，115200 |
| BLE link / wake / reset | PB0 / PB2 / PB10 | GPIO |
| 电池 ADC | PA4 | ADC1_IN4 |
| 充电状态 / 外部电源 | PB1 / PB11 | GPIO 输入 |
| 按键唤醒 | PB12 | EXTI 上升沿 |

## 构建

```bash
# STM32G030 主线
cd stm32/STM32Band_test
cmake --preset Debug
cmake --build build/Debug

# ESP32 旧硬件
cd esp32
idf.py build
idf.py -p <PORT> flash monitor
```

## 马达与 CubeMX 注意

1. 不要重复调用 `HAL_TIM_PWM_Start()`；第二次会静默失败，必要时直接操作 `TIM_CCER_CC1E` / `TIM_CR1_CEN`。
2. `motor_init()` 自带 GPIO AF 与 TIM16 重配，可安全用于上电、调试或看门狗复位后。
3. 占空比低于约 75% 时 MOSFET 与马达启动电流不足，可能完全不振。
4. 脉冲短于 300 ms 基本不可感知。
5. 从 `.ioc` 重新生成后，需要把 `driver/` 的项目内副本同步回 `Core/`。

## 文档

- [`stm32/doc/BLE_Signal_Protocol.md`](stm32/doc/BLE_Signal_Protocol.md)
- [`esp32/BLE_Signal_Protocol.md`](esp32/BLE_Signal_Protocol.md)
- [`esp32/BLE_API.md`](esp32/BLE_API.md)
- 主机侧契约：[`../../docs/protocols/EdgeFocus-最终API契约-主机手环.md`](../../docs/protocols/EdgeFocus-最终API契约-主机手环.md)
