# Hardware Bring-up Material Audit

Review date: 2026-07-12

## Verdict

The current `hardware_bringup` pack is reasonable to keep in the main project workspace for application development, but it must be treated as reference material, not as a starting point for code tasks.

The pack is about 19 MB and contains no full vendor Wi-Fi/Bluetooth trees, no large screen vendor package, no deployment binary package, and no large raw archives. It is small enough to keep locally.

## Why It Is Useful To Code Development

- It records which board features are reliable, incomplete, or absent.
- It explains why the app must tolerate missing Wi-Fi/Bluetooth, display, audio, and camera at runtime.
- It preserves the NPU/Galcore fix context needed for real inference mode.
- It keeps deployment and service startup context near the code workspace.
- It provides board topology and netlist references when a code feature depends on hardware availability.

## Keep As Primary Code Reference

- `CODE_AGENT_ENTRY.md`
- `status/progress.md`
- `status/findings.md`
- `docs/board_netlist_device_tree_notes.md`
- `docs/rtl8723du_bt_vendor_report.md`
- `artifacts/selected_logs_and_sources/focus_app.service`

## Keep As Deep Reference Only

- `clean_dtb_backup_20260630_214811/`
- `artifacts/dtb/`
- `artifacts/kernel/`
- `hardware/V2(3).tel`
- `logs/`
- `scripts/`
- `tools/`

These are retained because they may be needed to answer specific hardware-bound questions. They should not be opened during normal application feature work.

## Not Needed In This Pack

The following should remain only in the original hardware workspace unless a future task explicitly requires them:

- Full vendor driver/source archives.
- Large screen data packages.
- Generated deployment binaries.
- Audio waveform test artifacts.
- Temporary HTTP transfer folders beyond the small recorded references already copied.

## Maintenance Rule

When syncing new material from `D:\Aze\Documents\developMP257`, update `CODE_AGENT_ENTRY.md` if the material changes an app-facing assumption. Otherwise keep the new material in the original hardware workspace.
