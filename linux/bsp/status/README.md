# STM32MP257 Self-Carrier Bring-up Workspace

This workspace tracks the STM32MP257 self-developed carrier board bring-up.

## Current Status

- Boot on self-carrier: works.
- Wi-Fi: works with RTL8723DU module and persistent SSH at `root@192.168.10.173`.
- Bluetooth: works after installing matching RTL8723DU firmware/config.
- System service cleanup: done for the current self-carrier environment.
- USB camera: enumerates as UVC; basic capture validation still needs final pass.
- Audio: partially brought up. MAX98357A/I2S1 ALSA card enumerates, but playback ends in underrun/EIO and no sound was heard.
- SPI screen: partially brought up. `/dev/spidev0.0` exists with the expected old DTB SPI8 changes, but the screen stays dark. Hardware SPI8 also has a transfer timeout on some byte patterns.

## Important Entry Files

- `task_plan.md`: original task plan.
- `progress.md`: chronological work log.
- `findings.md`: accumulated technical findings and decisions.
- `SCREEN_BRINGUP_STATUS.md`: current screen-specific handoff/status.

## Directory Layout

- `artifacts/wifi/`: RTL8723DU Wi-Fi archives, extracted sources, built kernel modules, and packaged driver artifacts.
- `artifacts/bluetooth/`: Bluetooth firmware/source artifacts used during RTL8723DU BT bring-up.
- `artifacts/dtb/`: reserved for DTB/DTS artifacts. The active historical DTB directory currently remains at top level as `clean_dtb_backup_20260630_214811` because Windows reported it was in use during cleanup.
- `artifacts/kernel/`: target kernel config and kernel-related build artifacts.
- `backups/`: downloaded board backups.
- `docs/`: notes, reports, and bring-up documentation.
- `hardware/`: hardware manuals, rendered PDF pages, and the self-carrier netlist.
- `logs/`: captured boot logs and serial logs.
- `scripts/`: helper scripts used for board backup, NAS streaming, and remote driver builds.
- `tools/`: standalone test programs, currently including the SPI LCD test program.

## Clean-System Note

The board was originally flashed from a clean official ALIENTEK system, but the current board image is no longer pristine. It has self-carrier DTB changes, Wi-Fi/Bluetooth installation, service cleanup, audio experiments, and screen experiments.

The clean original DTB and old development-system screen DTB are preserved under `clean_dtb_backup_20260630_214811`.

