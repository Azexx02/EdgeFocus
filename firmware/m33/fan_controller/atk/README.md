# ATK STM32MP257D M33 fan firmware

This directory is a reproducible overlay for the vendor `OpenAMP_TTY_echo`
project. It does not vendor the full ATK BSP or commit CubeIDE caches.

The board adapter owns:

- PB10 / TIM17_CH1N / AF7, 25 kHz complementary PWM with the same duty
  semantics as the verified Linux prototype;
- optional PB11 / EXTI2 line 11 rising-edge tach counting, currently disabled
  until the carrier-board signal and former CAN conflict are measured;
- RIF resource acquisition/release for TIM17 and the enabled GPIO resources;
- OpenAMP VIRT_UART transport and the 100 ms portable control tick.

Build from PowerShell:

```powershell
firmware/m33/fan_controller/atk/build_atk.ps1
```

The script creates an isolated generated sibling beside the ATK example (so
all vendor paths remain valid), adds the portable fan core, and builds through
CMake/Ninja with CubeIDE's GNU Tools for STM32. This avoids a reproducible
access violation in CubeIDE 1.17's patched GNU Make when C++ is added to the
vendor C-only project. Final ELF files are copied to
`_workspace/b/m33fan/artifacts`; the stripped ELF is suitable for the board's
verified `remoteproc fw_format=ELF`.

The vendor manual is used for the CM33/OpenAMP build and launch skeleton only.
This product uses a modified carrier; PB10/PB11 mapping and electrical settings
come from the live v17 DT, pinctrl debug, netlist, and board measurements.

Do not load the firmware while Linux owns PB10/PB11. Follow the board switch
runbook: stop `fan-temp-control.service`, unexport/unbind Linux PWM ownership,
verify both pins have no Linux consumers, then start M33. Rollback always stops
M33 before restoring Linux ownership. The service is not a fan-safety
dependency: this board's fan continues spinning in its hardware-default state
when the service is stopped.

The vendor manual and `Utilities/scripts/fw_cortex_m33.sh` both identify M33
as `remoteproc0`, use an ELF when `fw_format` is `ELF`, and expose the first
VIRT_UART as `/dev/ttyRPMSG0`. Board operations are therefore explicit:

```powershell
python firmware/m33/fan_controller/atk/deploy_m33.py probe
python firmware/m33/fan_controller/atk/deploy_m33.py deploy --board 192.168.10.174 --expected-stage 9
python firmware/m33/fan_controller/atk/deploy_m33.py stop --board 192.168.10.174
```

`deploy` refuses a non-ELF remoteproc mode, uploads atomically, stops the old
bridge/M33/Linux PWM writers, disables `fan-temp-control.service`, unexports
PWM0, unbinds the Linux PWM child and timer parent, starts M33, and requires
both `state=running` and `/dev/ttyRPMSG0`. A failed start restores the Linux
driver bindings.

Bring-up status on `192.168.10.174` (2026-07-15): the staged CH1N path passes
stages 0-9, CC1NE/MOE/CEN readback, and a complete 100 ms control loop. The
previous whole-board reset was traced to `HAL_GetUIDw0()` from the non-secure
M33 domain; the firmware now derives a non-security boot nonce only from local
runtime counters. Do not reintroduce direct UID/OTP access without a verified
secure service.

The standalone A35 bridge is installed as
`/usr/local/bin/edgefocus-fan-bridge`. Its unit is intentionally static and
manual. On the validated board, M33 owns TIM17/PB10 while Linux timer/PWM
drivers remain unbound. `focus_app.service` stays independent. A clean bridge
stop sends `ShutdownNotice`; the next valid bridge HELLO re-establishes the
transport, M33 first reports `FAIL_SAFE`, and two valid 1 Hz temperature
samples restore `CONTROLLED`.

PB11 tach, M33 boot integration, and bridge autostart remain disabled. This is
a deliberate manual-validation state, not the final power-on deployment.
