# EdgeFocus Final Development Entry

This is the compact entry point for a new Codex session. It reflects the current project state after host architecture work, board migration, screen bring-up, and main-loop verification.

## Start Here

Primary repo:

```text
C:\Users\Aze\Desktop\BoardCode
```

Do not use `D:\Aze\Documents\developMP257` for this task. It is outside this workstream and is not the active git repo.

Read in this order:

1. `AGENTS.md`
2. `docs/FINAL_DEVELOPMENT_ENTRY.md`
3. `docs/SCREEN_BRINGUP_HANDOFF.md`
4. `findings.md`
5. `progress.md`

## Current Board Access

```text
Board IP: 192.168.10.210
Compile host: 192.168.10.6
Serial: COM7, 115200
Wi-Fi SSID used during setup: 105
```

The board is on the new SD-card development system. Do not enable `focus_app.service` autostart unless explicitly requested. Manual app launch is preferred during final development.

Manual start:

```sh
rfkill unblock bluetooth
bluetoothctl power on
cd /root
EDGEFOCUS_VISION_MODE=real EDGEFOCUS_LCD_INVERT_BL=0 nohup ./focus_app --log-level debug > /tmp/focus_app.log 2>&1 &
```

## Current Verified Main Loop

The following was verified on real board hardware:

1. App-facing GATT API responds through FF01/FF02.
2. `session_control start` creates a real session.
3. `session_control skip_precheck` moves the session to focusing.
4. Camera opens at index `7`, `640x480`, and captures frames.
5. Vision/NPU path initializes.
6. `phone_sync` with entertainment use participates in behavior analysis.
7. ReminderPolicy emits reminders.
8. Device layer executes:
   - screen flash
   - generated sound tone through ALSA `aplay`
   - BLE band Alert write to `0x2A56`
9. `pause` and `resume` work.
10. `end` generates a report.
11. `get_last_report`, `get_session_history`, and `session_feedback` work with the same `session_id`.

Last verified report chain used:

```text
session_id=session_1709055190198
distraction_count=1
phone_distraction_count=1
reminder_count=2
feedback=just_right
```

## Board Test Commands

Run all deployed ARM module tests:

```powershell
$env:EDGEFOCUS_BOARD_IP='192.168.10.210'
python deploy\run_board_test.py all
```

Run App-facing GATT smoke:

```powershell
$env:EDGEFOCUS_BOARD_IP='192.168.10.210'
python deploy\board_gatt_smoke.py --cmd get_current_state
```

Start a short session through the App API:

```powershell
$env:EDGEFOCUS_BOARD_IP='192.168.10.210'
python deploy\board_gatt_smoke.py --cmd session_control --data-b64 eyJhY3Rpb24iOiJzdGFydCIsInRhcmdldF9kdXJhdGlvbl9taW4iOjF9
```

The base64 payload is:

```json
{"action":"start","target_duration_min":1}
```

Use `--data-b64` for non-trivial payloads because PowerShell quoting can corrupt inline JSON.

## Final Development Priorities

Prioritize:

1. Preserve and polish the verified main loop.
2. Add/finish gimbal support through the sub-board serial protocol, but do not block the core session loop on gimbal perfection.
3. Keep camera/vision stable enough for demonstration.
4. Keep App-facing APIs consistent and report/history/feedback tied to one `session_id`.
5. Keep reminder channel rules correct:
   - normal: sound, screen flash, band vibration
   - silent: screen flash, band vibration
   - silent filters channels only; it does not lower severity

Do not prioritize:

- Dashboard rewrite
- calibration UI redesign
- calibration protocol changes
- band ACK/Notify/battery upstream
- cloud/server features
- over-polishing UI text

## Important Semantics

Phone state:

- `PHONE_IN_USE` and `PHONE_IN_PLAYING` are behavior-layer inputs.
- They do not directly force a fixed reminder level.
- `PHONE_IN_PLAYING` is not necessarily `LEVEL_3`.
- `PHONE_IN_USE` is not necessarily `LEVEL_2`.

Silent/normal:

- Severity is selected by behavior/policy.
- Mode only filters which reminder channels may be used.

No phone connected:

- Missing `phone_sync` must not block the session.
- Camera/vision path should continue as another distraction source.

## Screen Bring-up

The current screen works because the active DTB was switched to the old-system spidev DTB. Do not lose these files:

```text
C:\Users\Aze\Desktop\BoardCode\board_backup_original_system\extracted\boot\stm32mp257d-atk-ddr-2GB.dtb.spidev-pb0cs-copy
/boot/stm32mp257d-atk-ddr-2GB.dtb
```

See `docs/SCREEN_BRINGUP_HANDOFF.md` for LCD SPI/GPIO details.

## Residual Risks

- Board system time was observed as 2024; set time before a polished demo if report timestamps matter.
- Local Windows build may require a proper Visual Studio/nmake shell. Board ARM tests are the stronger verification path for this hardware target.
- `UIManager` logs `FrameBuffer: 240x320 (real=no)` on the working SPI LCD path. This is not a screen failure; it only means `/dev/fb0` mmap was not used.
