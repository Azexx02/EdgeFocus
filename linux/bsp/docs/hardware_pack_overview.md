# Self-Developed Board Hardware Bring-up Reference

This directory is the project-level hardware reference pack for the self-developed STM32MP257 carrier board.

For code-development agents: read `CODE_AGENT_ENTRY.md` first. Do not start from DTB/kernel/raw-log folders unless the user explicitly asks for low-level hardware work.

Source hardware exploration workspace:

`D:\Aze\Documents\developMP257`

Current project workspace:

`C:\Users\Aze\Desktop\self_developed`

Application code repository location:

`C:\Users\Aze\Desktop\self_developed\code\focus_app`

## Maintenance Note

This README is intentionally editable. When board status, deployment paths, service behavior, hardware availability, or known failure boundaries change, update this file together with the detailed records. Keeping this summary current prevents future code-development agents from relying on stale hardware assumptions.

## What This Pack Is For

This pack contains selected hardware bring-up material needed by application development agents to make correct runtime assumptions, deployment decisions, and feature gating choices for the self-developed STM32MP257 carrier board.

It is not the primary place to continue low-level hardware debugging. Continue that work in `D:\Aze\Documents\developMP257`.

## Deployment Hardware Notes

These are the practical board facts most likely to matter during application deployment.

### Board Access

- Serial console used during bring-up: `COM3`, `115200` baud.
- The board auto-login user observed during bring-up: `root`.
- Network IP is not fixed. After antenna re-solder and Wi-Fi recovery on 2026-07-13, the board obtained `192.168.10.174/24` on SSID `105`. Earlier working states used `192.168.10.171`; always verify current IP at runtime.
- Do not assume SSH is available after hardware changes or cold boot. If `wlan0` is missing, use serial first.

### Application Runtime

- Deployment path used during bring-up: `/opt/edgefocus`.
- Main service: `focus_app.service`.
- Service command observed in the adjusted unit:
  `/opt/edgefocus/focus_app --log-level info --demo-http-port 18080`
- Service dependency was changed away from a hard `network-online.target` dependency so the app can start even when Wi-Fi is absent.
- Real inference mode depends on `/dev/galcore` being present.

Useful checks on the board:

```sh
systemctl status focus_app.service --no-pager
systemctl is-active focus_app.service
ls -l /dev/galcore
ip -br link
lsusb
lsusb -t
ls /sys/class/net
```

### Hardware Capability Snapshot

Treat these as runtime-detected capabilities, not constants hardcoded into the app.

| Area | Current app-facing status | Code/deployment implication |
| --- | --- | --- |
| NPU/GPU | Previously recovered and verified through Galcore; `/dev/galcore` existed in v16B state. | Real inference can run when `/dev/galcore` exists; fall back or fail clearly if absent. |
| Wi-Fi | RTL8723DU USB module behind CH334R hub. Recovered on 2026-07-13 after connector cleaning, Wi-Fi re-solder, and antenna re-solder; verified as `wlan0 UP 192.168.10.174/24`, SSID `105`, signal about `-67 dBm`. | Detect `wlan0` and IP at runtime. App/deployment should still tolerate serial-only access after hardware changes. |
| Bluetooth | Same RTL8723DU module as Wi-Fi. `hci0` was verified `UP RUNNING` after 2026-07-13 recovery. | Treat adapter as delayed/optional at startup; handle missing Bluetooth cleanly. |
| USB camera | Previously enumerated and captured frames in a working state. | Detect `/dev/video*` at runtime. Do not assume camera is always present. |
| Fan | PB10 is the 25 kHz fan PWM output, PB11 is the fan tach input. Verified on 2026-07-15 with v17 DTB and `fan-temp-control.service`. | Fan control can use `/sys/class/pwm/pwmchip0/pwm0`; tach uses GPIOB line 11. PB10 duty is inverted by the board driver circuit: lower duty means faster fan. |
| Local display | MIPI/SPI display bring-up was incomplete. | Do not require local display for core app operation unless hardware notes are updated. |
| Speaker/audio | MAX98357A path was partially proven but no stable audible output conclusion. | Do not require speaker output; keep audio behavior optional. |
| Ethernet | Self-developed carrier board has no wired Ethernet. | Do not rely on `eth*` for deployment or app networking. |

### Fan Boundary

Fan bring-up was solidified on 2026-07-15:

```text
DTB -> stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v17-fan-pb10-pwm-pb11-tach.dtb
SHA256 -> 9a6404656a6ed354f22d16d1cb1ea800ef064bbb876a0274feee08eb426e99e5
PB10 -> /sys/class/pwm/pwmchip0/pwm0, 25 kHz, active through inverted external driver
PB11 -> GPIOB line 11 tach input
```

Important device-tree trade-off:

- `can@402d0000` was disabled by changing its status from `okay` to `fail`.
- Reason: that M_CAN instance claimed PB11 as AF7, which blocked fan tach reads.
- Effect: `/sys/class/net` now shows two CAN interfaces instead of three. In the verified boot after v17, it showed `can0 can1 lo wlan0`.
- This is intentional. If a later design needs that CAN instance, PB11 fan tach and `can@402d0000` must be pinmuxed differently.

Runtime fan files on the board:

```sh
/usr/local/bin/fan_temp_control.py
/etc/systemd/system/fan-temp-control.service
```

Useful checks:

```sh
systemctl status fan-temp-control.service --no-pager
journalctl -u fan-temp-control.service -n 20 --no-pager
cat /sys/class/pwm/pwmchip0/pwm0/period
cat /sys/class/pwm/pwmchip0/pwm0/duty_cycle
gpioinfo -c gpiochip1 10 11
```

### Wi-Fi/Bluetooth Boundary

Latest known-good state after 2026-07-13 hardware recovery:

```text
lsusb -> CH334R hub 1a86:8091 and RTL8723DU 0bda:d723
lsusb -t -> rtk_btusb on interface 0/1, rtl8723du on interface 2
wlan0 -> UP 192.168.10.174/24
SSID -> 105
signal -> about -67 dBm
hci0 -> UP RUNNING
ping 192.168.10.1 -> 0% loss
ping 223.5.5.5 -> 0% loss
```

Failure boundary learned during bring-up:

```text
lsusb -> only Linux Foundation 2.0 root hub
/sys/class/net -> can0 can1 can2 lo
```

Meaning: CH334R USB hub did not enumerate, so the RTL8723DU Wi-Fi/BT module was not visible to Linux. In that state, changing Wi-Fi credentials, wpa_supplicant, Bluetooth code, or Realtek firmware will not fix the problem.

Root cause status as of 2026-07-13:

- Hub disappearance recovered after cleaning the core-board/carrier connector area, so the hub-level fault is strongly correlated with connector/contact contamination or upstream USBH_D_N/P contact path.
- Wi-Fi WPA/DHCP failure recovered after antenna re-solder, so the remaining link issue was consistent with antenna/RF path weakness.
- Full detail is in `docs/ch334r_enumeration_regression_audit.md`.

Software mitigation history:

- Temporary mitigations were once installed to disable RTL8723DU USB power saving and delay `rtk_btusb` loading.
- These mitigations were rolled back on 2026-07-12 at the user's request.
- Current direct startup state after rollback: `bluetooth.service` and `bluealsa.service` are enabled; `rtk-btusb-delayed.service` is not active/present as a service unit.
- This rollback does not change the main hardware boundary: CH334R must enumerate before RTL8723DU Wi-Fi/Bluetooth can appear.

### NPU/Galcore Boundary

The NPU issue was solved by enabling the Vivante/Galcore device-tree node. The app-facing check is simple:

```sh
ls -l /dev/galcore
```

If `/dev/galcore` is absent, real NPU inference will fail even if model files and user-space libraries exist.

### Camera Boundary

The USB camera was previously verified with `/dev/video0` and `/dev/video1` in a working board state. Code should enumerate camera devices and handle absence. Avoid binding product behavior to a fixed video index unless deployment scripts verify it on the target boot.

### Display And Audio Boundary

Local board display and speaker output are not stable product assumptions yet. Application code should be able to run headless and without local audio. UI/feedback behavior should degrade to network/BLE/log based output unless a later hardware note updates this status.

## Recommended Entry

Read in this order:

1. `CODE_AGENT_ENTRY.md`
2. `status/progress.md`
3. `docs/board_netlist_device_tree_notes.md`
4. Only then open deeper folders if the task explicitly requires them.

## Contents

- `CODE_AGENT_ENTRY.md`: guardrail and reading map for code-development agents.
- `MATERIAL_AUDIT.md`: why this pack is kept and which folders are primary vs deep references.
- `status/`: current summaries, progress notes, findings, screen/audio status, and previous task plans.
- `docs/`: concise board notes, install notes, Wi-Fi/Bluetooth report, CH334R/RTL8723DU regression audit, and selected plans.
- `hardware/`: board netlist and hardware manual excerpts used during bring-up.
- `logs/`: raw boot and diagnostic logs.
- `scripts/`: helper scripts used during backup, Wi-Fi driver build, and board data capture.
- `tools/`: local display/framebuffer test utilities from bring-up.
- `artifacts/dtb/`: selected DTB/DTS variants, including v16B and earlier known rollback points.
- `artifacts/kernel/`: selected kernel/module source snippets from display/audio investigation.
- `artifacts/selected_logs_and_sources/`: small selected logs and source snippets most likely to be useful later.
- `clean_dtb_backup_20260630_214811/`: clean/original/selfcarrier DTB and boot configuration backups.

## Review Result

The current set is acceptable for the project-level reference pack because it is small enough to keep locally and contains the hardware facts needed by app development. The risky part is not size; it is navigation. Code agents should not browse DTB/kernel/log folders by default. Use `CODE_AGENT_ENTRY.md` as the gate.

## Not Copied By Default

Large raw archives, full Wi-Fi/Bluetooth vendor trees, full screen vendor packages, deployment binaries, and generated audio waveform files remain in the original hardware workspace.




