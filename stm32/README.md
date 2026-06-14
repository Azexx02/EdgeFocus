# STM32-Band

基于 STM32G030C8T6 的蓝牙手环，功能与 ESP32-Band 保持一致。

## 当前进度

| 日期 | 任务 | 状态 |
|------|------|------|
| 6月11日 | 驱动震动马达 (PA6) + OLED屏幕 (PB6/PB7) | 已完成 |
| 6月12日 | PWM马达驱动 + BLE协议响应测试 | 已完成 |
| 待定 | E104-BT52 蓝牙模块通信 (PA9/PA10) | 未开始 |

## 文件结构

```
stm32/
├── driver/                    # 可复用底层驱动（正式开发用）
│   ├── ssd1306.h             # OLED 驱动头文件
│   ├── ssd1306.c             # OLED 驱动实现 (含倒计时大字显示)
│   ├── motor.h               # 马达驱动头文件 (PWM)
│   └── motor.c               # 马达驱动实现 (TIM16 PWM, 5kHz)
├── STM32Band_test/
│   ├── Core/
│   │   ├── Inc/
│   │   │   ├── main.h
│   │   │   ├── ssd1306.h
│   │   │   ├── motor.h
│   │   │   ├── test_motor_oled.h
│   │   │   └── test_ble_response.h
│   │   └── Src/
│   │       ├── main.c
│   │       ├── ssd1306.c
│   │       ├── motor.c
│   │       ├── test_motor_oled.c
│   │       └── test_ble_response.c  # BLE协议响应测试
│   └── Drivers/               # STM32 HAL/CMSIS 库
├── BLE_Signal_Protocol.md     # BLE 信号协议文档
├── README.md                  # 本文件
└── doc/                       #引脚定义及相关文档
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

## 马达驱动说明 (motor.c / motor.h)

已升级为 PWM 驱动 (TIM16_CH1, PA6):
- PWM 频率: 5kHz (HSI 16MHz / 3200)
- 支持 0~100% 占空比调速
- `motor_set_duty(percent)` 控制振动强度
- `motor_vibrate(ms)` 阻塞式振动指定时长
- 最低启动占空比: ~75%（MOSFET gate threshold + 电机启动电流）

### motor_init() 设计说明

`motor_init()` 是**完全自包含**的，不依赖 CubeMX 的初始化顺序。内部会：
1. 使能 GPIOA 时钟 (`RCC->IOPENR`)
2. 配置 PA6 为 AF5 (TIM16_CH1)
3. 使能 TIM16 时钟 (`RCC->APBENR2`)
4. 重配定时器参数 (PSC=0, ARR=3199, PWM mode 1)
5. 使能 MOE + 启动计数器

这样无论是上电复位、debug reset、还是看门狗复位，调用 `motor_init()` 都能正常工作。

### 踩坑记录

| 问题 | 原因 | 解决 |
|------|------|------|
| `HAL_TIM_PWM_Start()` 重复调用后失效 | HAL 内部状态机 `READY→BUSY`，重复调用返回错误 | 改用寄存器直接操作 |
| VS Code debug reset 后马达不振 | 调试器只做软复位，不清除 GPIO AF 配置 | `motor_init()` 强制重配所有寄存器 |
| 25% duty 不振 | 低于 MOSFET/motor 启动阈值 | 使用 ≥75% duty |
| 150ms 脉冲感知不到 | 马达启动需要时间 | 脉冲 ≥300ms |

## BLE 协议响应测试

`test_ble_response.c` 按照 `BLE_Signal_Protocol.md` 测试以下功能:

1. **Alert (0x01)** — 三种震动模式
   - 0x01: 连续缓慢短震 (3次, 间隔500ms)
   - 0x02: 长振 (800ms)
   - 0x03: 连续快速短振 (3次, 间隔100ms)
2. **TimeSync (0x02)** — OLED 倒计时显示 (大字 MM:SS)
3. **Text (0x03)** — OLED 文字显示

当前未接入 BLE，通过直接调用处理函数模拟协议命令。

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
