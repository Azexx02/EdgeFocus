# AGENTS.md

## Project overview

BLE wristband firmware. This is an extension of `C:\Users\Aze\Desktop\BoardCode` (STM32MP2 host). The host communicates with the wristband via BLE GATT (Service `0x180C`, Characteristic `0x2A56`).

`esp32/` is the **old version** (ESP32-S3 dev board). Active development is on **STM32G030C8T6**.

| Directory | Role | Toolchain | Status |
|-----------|------|-----------|--------|
| `stm32/stm32BLE_Band/` | **Main production firmware** | STM32CubeIDE | Bare CubeMX scaffold, awaiting driver integration |
| `stm32/STM32Band_test/` | **Test harness** for module validation | CMake + Ninja + GCC ARM | OLED + motor working, BLE pending |
| `esp32/` | Legacy reference (old hardware) | ESP-IDF + NimBLE | BLE working |

## Development workflow

1. **Write & validate drivers/tests in `STM32Band_test/`** — this is the fast iteration loop.
2. **Port proven drivers from `STM32Band_test/driver/` into `stm32BLE_Band/`** — copy `motor.h/c`, `ssd1306.h/c` into `Core/Inc/` and `Core/Src/`.
3. **If flashing `stm32BLE_Band` fails**, reproduce the issue in `STM32Band_test/` first, fix there, then re-port.
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

## STM32 test harness (`stm32/STM32Band_test/`)

CMake project with Ninja + `gcc-arm-none-eabi`.

- **Configure**: `cmake --preset Debug` (from `stm32/STM32Band_test/`)
- **Build**: `cmake --build build/Debug`
- **IDE**: STM32CubeIDE — open `.ioc` to regenerate CubeMX config

Key files:
- `driver/` — **reusable source of truth** (motor.h/c, ssd1306.h/c). Copy into `Core/` before building.
- `Core/Src/main.c` — CubeMX-generated; user code ONLY in `/* USER CODE BEGIN/END */` blocks.
- `Core/Src/test_motor_oled.c` — OLED + motor hardware validation
- `Core/Src/test_ble_response.c` — BLE protocol handler tests (simulated, no real BLE)

## STM32 main firmware (`stm32/stm32BLE_Band/`)

STM32CubeIDE project (no CMake). Currently a bare CubeMX scaffold with all peripherals configured but no application code.

- Open `stm32BLE_Band.ioc` in STM32CubeIDE to regenerate.
- Additional hardware vs test project: charging status (PB1), external power validity (PB11), button wake (PB12, EXTI).
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

- Regenerate from `.ioc` before editing `main.c` — then re-copy driver files from `driver/` into `Core/`.
- `driver/` files are the source of truth. `Core/Inc/` and `Core/Src/` are copies that may be stale.
- I2C1 configured at 400kHz Fast Mode in both `.ioc` files.
