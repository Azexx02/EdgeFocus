# STM32-Band

基于 STM32G030C8T6 的蓝牙手环，功能与 ESP32-Band 保持一致。

## 当前进度

| 日期 | 任务 | 状态 |
|------|------|------|
| 6月11日 | 驱动震动马达 (PA6) + OLED屏幕 (PB6/PB7) | 进行中 |
| 待定 | E104-BT52 蓝牙模块通信 (PA9/PA10) | 未开始 |

## 文件结构

```
stm32/
├── Core/
│   ├── Inc/
│   │   └── main_app.h        # 应用层头文件
│   └── Src/
│       └── main_app.c        # 主程序（初始化 + 主循环）
├── Drivers/
│   ├── ssd1306.h             # OLED 驱动头文件
│   ├── ssd1306.c             # OLED 驱动实现
│   ├── motor.h               # 马达驱动头文件
│   └── motor.c               # 马达驱动实现
├── test/
│   ├── test_motor_oled.h     # 硬件测试头文件
│   └── test_motor_oled.c     # 硬件测试程序
├── doc/
│   └── 引脚对照表.md          # 硬件引脚定义
└── README.md                 # 本文件
```

## CubeMX 配置步骤

### 1. 创建工程

- 打开 STM32CubeIDE，新建 STM32 工程
- 选择芯片: **STM32G030C8Tx**
- 命名工程: `BLEband`

### 2. 时钟配置

- HSE: 选择外部晶振（如有）或使用 HSI 64MHz
- SYSCLK: 64 MHz
- AHB Prescaler: 1
- APB Prescaler: 1

### 3. 引脚配置

#### I2C1 (OLED)

| 引脚 | 功能 | CubeMX 配置 |
|------|------|-------------|
| PB6 | I2C1_SCL | I2C1 → SCL |
| PB7 | I2C1_SDA | I2C1 → SDA |

I2C1 参数:
- Speed: Fast Mode (400 kHz)
- Addressing: 7-bit
- Own Address: 0 (主模式)

#### GPIO Output (马达)

| 引脚 | 功能 | CubeMX 配置 |
|------|------|-------------|
| PA6 | MOTOR | GPIO_Output, 推挽, 默认低电平, 无上下拉 |

#### GPIO Input (BLE 模块)

| 引脚 | 功能 | CubeMX 配置 |
|------|------|-------------|
| PB0 | BLE_LINK | GPIO_Input, 无上下拉 |
| PB2 | BLE_WKP | GPIO_Output, 推挽, 默认低电平 |
| PB10 | BLE_RST | GPIO_Output, 推挽, 默认高电平 |

#### USART1 (BLE 模块串口)

| 引脚 | 功能 | CubeMX 配置 |
|------|------|-------------|
| PA9 | BLE_TX | USART1_TX |
| PA10 | BLE_RX | USART1_RX |

USART1 参数:
- Baud Rate: 115200
- Data Bits: 8
- Stop Bits: 1
- Parity: None
- 开启 NVIC 中断（接收用）

#### ADC (电池电压)

| 引脚 | 功能 | CubeMX 配置 |
|------|------|-------------|
| PA4 | BATTERY | ADC1_IN4 |

### 4. 工程设置

- Project Manager → Toolchain/IDE: STM32CubeIDE
- 生成代码

### 5. 添加驱动文件

将 `Drivers/` 目录下的文件复制到工程中:
- `ssd1306.h` / `ssd1306.c` → `Core/Inc/` 和 `Core/Src/`
- `motor.h` / `motor.c` → `Core/Inc/` 和 `Core/Src/`
- `main_app.h` / `main_app.c` → `Core/Inc/` 和 `Core/Src/`

### 6. 修改 main.c

在 CubeMX 生成的 `main.c` 中:

```c
/* USER CODE BEGIN Includes */
#include "main_app.h"
/* USER CODE END Includes */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();
    MX_ADC1_Init();

    /* USER CODE BEGIN 2 */
    main_app();  // 进入应用主程序
    /* USER CODE END 2 */

    while (1) {}
}
```

## 硬件测试程序

`test/test_motor_oled.c` 提供独立的硬件测试，验证 OLED 和马达是否正常工作。

### 使用方法

1. 将 `test/test_motor_oled.h` 和 `test/test_motor_oled.c` 复制到工程的 `Core/Inc/` 和 `Core/Src/`
2. 修改 `main.c`:

```c
/* USER CODE BEGIN Includes */
#include "test_motor_oled.h"
/* USER CODE END Includes */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_I2C1_Init();

    /* USER CODE BEGIN 2 */
    test_main();  // 运行硬件测试
    /* USER CODE END 2 */

    while (1) {}
}
```

### 测试流程

1. **OLED 测试** - 显示字符、字母数字、滚动效果、像素绘制
2. **马达测试** - 短震(100ms)、中震(300ms)、长震(800ms)、SOS 摩尔斯码

测试完成后屏幕显示各模块状态，马达双短震表示全部通过。

## 马达驱动说明

当前使用 GPIO 直驱（与 ESP32 版本一致）:
- PA6 HIGH → 马达震动
- PA6 LOW → 马达停止

如需 PWM 调速，可后续启用 TIM16_CH1 (PA6) 进行 PWM 输出。

## 与 ESP32-Band 的兼容性

| 功能 | ESP32-Band | STM32-Band | 状态 |
|------|-----------|-----------|------|
| OLED 显示 | SSD1306 I2C (GPIO8/9) | SSD1306 I2C (PB6/PB7) | 驱动已移植 |
| 马达震动 | GPIO4 | PA6 | 驱动已移植 |
| BLE 服务 | NimBLE (内置) | E104-BT52 (UART) | 待实现 |
| BLE UUID | 0x180C / 0x2A56 | 0x180C / 0x2A56 | 保持一致 |
| 连接状态显示 | OLED 实时更新 | OLED 实时更新 | 已实现 |
| 信号接收震动 | 500ms | 500ms | 已实现 |
| 启动震动 | 100ms | 100ms | 已实现 |
