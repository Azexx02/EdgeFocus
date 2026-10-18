# Code Agent Entry: Self-Developed Board Reference

This is the first file to read when working on application code for the self-developed STM32MP257 board.

## Purpose

Use this directory only to understand board constraints that affect application code, deployment, and runtime feature gating. Do not start hardware bring-up from here.

The low-level hardware exploration workspace remains:

`D:\Aze\Documents\developMP257`

The application code repository is intended to be:

`C:\Users\Aze\Desktop\self_developed\code\focus_app`

## Read This First

1. `status/progress.md`
   - Current board status and historical decisions.
   - Start from these sections if present:
     - `2026-07-12 Project workspace split`
     - `2026-07-08 Wi-Fi/BT USB hub regression check`
     - `2026-07-05 MAX98357A playback attempt`
     - `2026-07-05` NPU/Galcore notes

2. `docs/board_netlist_device_tree_notes.md`
   - High-level hardware topology that matters to software.
   - Use this for USB hub, Wi-Fi/BT, audio, camera, and board feature assumptions.

3. `docs/ch334r_enumeration_regression_audit.md`
   - Latest CH334R/RTL8723DU failure and recovery record.
   - Use when checking whether Wi-Fi/Bluetooth absence is hardware, driver, or app-layer related.

4. `docs/rtl8723du_bt_vendor_report.md`
   - Bluetooth/Wi-Fi driver and firmware context.
   - Use only when touching Bluetooth/Wi-Fi application behavior or startup sequencing.

5. `artifacts/selected_logs_and_sources/focus_app.service`
   - Reference systemd service configuration captured during bring-up.
   - Use when changing deployment or service startup behavior.

6. `status/SCREEN_BRINGUP_STATUS.md` and `status/MIPI_DSI_DEBUG_ANALYSIS.md`
   - Read only if application code needs to know why local display support is disabled or incomplete.

## Current Board Facts For App Development

- NPU/GPU path was recovered by enabling the Vivante/Galcore DT node. `/dev/galcore` existed in the last verified v16B state.
- The current verified DTB after fan bring-up is v17:
  `stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v17-fan-pb10-pwm-pb11-tach.dtb`
  with SHA256 `9a6404656a6ed354f22d16d1cb1ea800ef064bbb876a0274feee08eb426e99e5`.
- Wi-Fi/Bluetooth are on the RTL8723DU USB module behind a CH334R USB hub.
- Latest known-good state on 2026-07-13: CH334R and RTL8723DU enumerated, `wlan0 UP 192.168.10.174/24`, SSID `105`, signal about `-67 dBm`, `hci0 UP RUNNING`, gateway and public-IP ping passed.
- The recent blocker was not a Wi-Fi driver failure: first the CH334R hub itself did not enumerate, then Wi-Fi was unstable until the antenna was re-soldered. Application code should still tolerate missing `wlan0` and missing/delayed Bluetooth adapter after hardware changes.
- Fan control is available after 2026-07-15 v17 DTB:
  PB10 is `/sys/class/pwm/pwmchip0/pwm0`; PB11 is GPIOB line 11 tach. `fan-temp-control.service` is enabled and runs `/usr/local/bin/fan_temp_control.py`. The fan PWM path is inverted: lower duty means faster fan.
- v17 intentionally disables `can@402d0000` to release PB11 from M_CAN AF7. Expect two CAN network interfaces instead of three in this DTB state.
- USB camera previously enumerated and captured frames in a working board state. Treat camera availability as runtime-detected, not guaranteed.
- Board-local MIPI display and MAX98357A audio were not fully finished. Do not assume display or speaker output is available unless a later hardware note says so.
- `focus_app.service` was adjusted away from hard `network-online.target` dependency so the app can start even when Wi-Fi is absent.

## Do Not Start Here Unless Explicitly Asked

These areas are deep hardware references. They are useful, but they can trap a code-focused task in board bring-up details.

- `clean_dtb_backup_20260630_214811/`
  - DTB/DTS rollback history and clean-system backups.
  - Use only for device-tree recovery or comparison.

- `artifacts/dtb/`
  - DTB/DTS variants from GPU, audio, and MIPI experiments.
  - Use only if the task is explicitly about DTB changes.

- `artifacts/kernel/`
  - Panel driver variants and kernel snippets.
  - Use only for kernel/module work.

- `hardware/V2(3).tel`
  - Full board netlist.
  - Use only when checking exact pins/nets. Prefer `docs/board_netlist_device_tree_notes.md` first.

- `logs/`
  - Raw serial logs.
  - Use only to verify a specific boot symptom or compare known-good/known-bad behavior.

- `scripts/` and `tools/`
  - Board bring-up helpers.
  - Use only when deploying, backing up, or reproducing a hardware test.

## Code-Agent Rules

- Do not modify DTB, kernel modules, serial boot configuration, or board services unless the user explicitly asks for hardware bring-up or deployment changes.
- For application changes, prefer runtime detection and graceful degradation over assuming hardware availability.
- If a feature depends on Wi-Fi, Bluetooth, camera, NPU, display, or audio, check the app's runtime capability layer before touching hardware notes.
- If hardware behavior is unclear, summarize the needed hardware question and stop at the boundary instead of digging through all DTB/kernel variants.
- Keep new code-development scratch files in `code/focus_app/_workspace/`, not in `hardware_bringup/`.

## Quick Decision Map

| Task type | Read | Avoid |
| --- | --- | --- |
| App deployment/service startup | `artifacts/selected_logs_and_sources/focus_app.service`, `status/progress.md` | DTB/kernel folders |
| Bluetooth/Wi-Fi application behavior | `docs/ch334r_enumeration_regression_audit.md`, `docs/rtl8723du_bt_vendor_report.md`, Wi-Fi/BT section in `status/progress.md` | Full vendor trees, DTB variants |
| NPU inference code | NPU/Galcore sections in `status/findings.md` and `status/progress.md` | MIPI/audio logs |
| Camera code | `status/progress.md` function check notes | Panel driver variants |
| Display/audio UI decisions | `status/SCREEN_BRINGUP_STATUS.md`, audio section in `status/progress.md` | Kernel driver experiments unless asked |
| Pin/net confirmation | `docs/board_netlist_device_tree_notes.md` first, then `hardware/V2(3).tel` | Raw logs |

