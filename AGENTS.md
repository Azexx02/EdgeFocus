# AGENTS.md

## Project overview

BLE wristband firmware. This is an extension of `C:\Users\Aze\Desktop\BoardCode` (STM32MP2 host). The host communicates with the wristband via BLE GATT (Service `0x180C`, Characteristic `0x2A56`).

`esp32/` is the **old version** (ESP32-S3 dev board). Active development is on **STM32G030C8T6**.

| Directory | Role | Toolchain | Status |
|-----------|------|-----------|--------|
| `stm32/STM32Band_test/` | **Main firmware and test harness** | CMake + Ninja + GCC ARM | OLED, motor, battery, power, button, BLE integrated |
| `stm32/Drivers/` | Reusable STM32 modules | C / STM32 HAL | Source of truth for portable band modules |
| `stm32/stm32BLE_Band/` | Archived production scaffold | STM32CubeIDE | Removed after `STM32Band_test` became the main project |
| `esp32/` | Legacy reference (old hardware) | ESP-IDF + NimBLE | BLE working |

## Development workflow

1. **Develop and validate in `stm32/STM32Band_test/`** — this is now both the fast iteration loop and the flashable main firmware.
2. **Keep reusable code in `stm32/Drivers/`** — mirror proven modules into `STM32Band_test/driver/` and project `Core/` copies as needed.
3. **If flashing or hardware behavior fails**, reproduce and fix it in `STM32Band_test/` first.
4. **Logic & text references** — check `esp32/` for protocol behavior and OLED文案 to keep consistent.

## ESP32 (`esp32/`) — reference only

ESP-IDF project. `$IDF_PATH` must be set.

- **Build**: `idf.py build` (run from `esp32/`)
- **Flash**: `idf.py -p <PORT> flash`
- **Monitor**: `idf.py -p <PORT> monitor`

Key files:
- `esp32/main/main.c` — single source; BLE, OLED, motor all here
- `esp32/components/ssd1306/` — custom I2C SSD1306 driver (GPIO8=SCL, GPIO9=SDA, addr 0x3C)
- `esp32/BLE_Signal_Protocol.md` — protocol spec (host interaction, frame format)
- `esp32/BLE_API.md` — client-side examples (Arduino + ESP-IDF NimBLE)

## STM32 main firmware (`stm32/STM32Band_test/`)

CMake project with Ninja + `gcc-arm-none-eabi`.

- **Configure**: `cmake --preset Debug` (from `stm32/STM32Band_test/`)
- **Build**: `cmake --build build/Debug`
- **IDE**: STM32CubeIDE — open `.ioc` to regenerate CubeMX config

Key files:
- `driver/` — project-local copies of reusable modules from `stm32/Drivers/`.
- `Core/Src/main.c` — CubeMX-generated; user code ONLY in `/* USER CODE BEGIN/END */` blocks.
- `Core/Src/test_ble.c` — current band application entry used for firmware bring-up.
- `Core/Src/test_solder_check.c` — solder/hardware validation path.

## Archived STM32 scaffold (`stm32/stm32BLE_Band/`)

The old CubeIDE scaffold was removed after `STM32Band_test` became the main flashable project.

- Additional hardware vs early test project: charging status (PB1), external power validity (PB11), button wake (PB12, EXTI).
- BLE module: E104-BT52 via UART1 (PA9/PA10, 115200), wake pin PB2, reset pin PB10.

## Motor driver gotchas (STM32)

Hard-won, do not skip:

1. **Never call `HAL_TIM_PWM_Start()` twice** — HAL state machine goes READY→BUSY, second call returns error silently. Use `TIM_CCER_CC1E` / `TIM_CR1_CEN` direct register ops.
2. **`motor_init()` is self-contained** — reconfigures GPIO AF + TIM16 from scratch. Safe after power-on, debug, or WDT reset. No CubeMX init order dependency.
3. **Minimum duty cycle: ≥75%** — MOSFET gate threshold + motor starting current. Below ~75% = no vibration.
4. **Minimum pulse duration: ≥300ms** — motor spin-up time; shorter pulses are imperceptible.
5. **PWM config**: TIM16_CH1 on PA6, 5kHz (PSC=0, ARR=3199), Mode 1.

## BLE protocol

Defined in `esp32/BLE_Signal_Protocol.md` and `stm32/doc/BLE_Signal_Protocol.md`. Wire format:

```
Byte 0: type (0x01=Alert, 0x02=TimeSync, 0x03=Text)
Byte 1-N: type-specific payload
```

- 1-byte writes = legacy Alert (short buzz, level 1).
- Alert patterns: 0x00=none, 0x01=slow 3x, 0x02=long, 0x03=fast 3x.
- TimeSync: big-endian minutes `(Byte1 << 8) | Byte2`.
- Text: UTF-8 after type byte.
- Host is STM32MP2 (BoardCode project). Wristband is BLE peripheral.

## Pin mapping (STM32G030C8T6)

| Function | Pin | Config |
|----------|-----|--------|
| OLED SDA | PB7 | I2C1 |
| OLED SCL | PB6 | I2C1, 400kHz Fast Mode |
| Motor | PA6 | TIM16_CH1 PWM |
| BLE UART TX | PA9 | USART1, 115200 |
| BLE UART RX | PA10 | USART1 |
| BLE link status | PB0 | GPIO input |
| BLE wake | PB2 | GPIO output |
| BLE reset | PB10 | GPIO output, default high |
| Battery ADC | PA4 | ADC1_IN4 |
| Charging status | PB1 | GPIO input |
| External power | PB11 | GPIO input |
| Button wake | PB12 | EXTI rising edge |

## STM32CubeMX gotchas

- Regenerate from `.ioc` before editing `main.c` — then re-copy project-local driver files from `driver/` into `Core/`.
- `stm32/Drivers/` is the reusable source of truth. `STM32Band_test/driver/` and `Core/` contain project copies.
- I2C1 configured at 400kHz Fast Mode in both `.ioc` files.
