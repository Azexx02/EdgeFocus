# M33 fan controller integration boundary

`FanControllerApp` is the portable, host-tested M33 control task. It deliberately contains no STM32 register addresses, pinmux, RIF, resource table, HAL handle, OpenAMP endpoint creation, scheduler, or watchdog initialization.

The board-specific STM32CubeMP2 project must provide three callbacks/events:

1. `write_pwm_duty_ns`: PB10 timer PWM write using a verified 40,000 ns period. The first call during `initialize()` must apply 8,000 ns before OpenAMP can block or fail.
2. `send_rpmsg_frame`: one complete protocol frame to the dedicated fan endpoint. The receive callback passes one complete frame to `onRpmsgFrame()`.
3. Periodic calls: `tick()` faster than 250 ms, and `onTachWindow()` after each non-blocking PB11 capture window.

Only feed M33 WWDG when `healthyForWatchdog()` is true and the board adapter has independently confirmed the scheduler/PWM task health.

## Deliberately unresolved hardware work

Do not fill these from assumptions. Import them from the matching STM32MP257D-ATK / OpenSTLinux BSP and verify on the running board:

- PB10 TIM instance, channel and alternate function;
- PB11 input capture or EXTI mapping, pulses per revolution, and CAN conflict;
- RIF ownership, clocks, pinctrl/device-tree changes and reset-time electrical state;
- OpenAMP resource table and dedicated endpoint registration;
- exact STM32CubeMP2 version/toolchain and WWDG clock/timing.

The current board Starter Package contains only `OpenAMP_TTY_echo_CM33_NonSecure.elf`, not the source project. Until the matching source package is supplied, this directory is intentionally not presented as a flashable firmware image.

## Host verification

The fan module has an isolated build that does not fetch or link product dependencies:

```bash
cmake -S platform/fan -B build/fan
cmake --build build/fan
ctest --test-dir build/fan --output-on-failure
```
