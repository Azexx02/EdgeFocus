# Backup and Clean Flash Plan

## Current Judgment

The current system has two likely problems mixed together:

- User-space/systemd state from previous development work waits for `wlan0`, `wlan1`, and a Wi-Fi autoconnect service.
- The boot log shows repeated USB enumeration failures on `usb 1-1.3` with `error -71`, which happens before a Wi-Fi driver can bind.

A clean image is useful to remove old services and config. If the clean image still reports `usb 1-1.3 error -71`, focus on board-level USB power/reset/device-tree differences.

## Lightweight Backup To USB

Insert a USB disk. On the board serial console:

```sh
lsblk
mount
```

Find the USB mount point. Common examples:

- `/run/media/sda1`
- `/media/sda1`
- `/mnt`

Copy or paste `scripts/board_backup_to_usb.sh` to the board as `/root/board_backup_to_usb.sh`, then run:

```sh
sh /root/board_backup_to_usb.sh /run/media/sda1
```

Change `/run/media/sda1` to the actual USB mount point.

This backs up:

- `/boot`
- `/etc`
- systemd units
- udev/module/network/Wi-Fi config
- firmware directories
- `/root` and app scripts
- running device tree sysfs snapshot
- USB/network/systemd/dmesg diagnostics

## Optional Full Disk Backup

Only do this if the target USB disk is larger than the source eMMC/SD.

First inspect devices carefully:

```sh
lsblk -a -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS
cat /proc/cmdline
findmnt /
```

Then identify the root disk, for example `/dev/mmcblk1`.

Example full image command:

```sh
dd if=/dev/mmcblk1 of=/run/media/sda1/mmcblk1_full_backup.img bs=4M status=progress conv=fsync
sync
```

Do not run the `dd` command until the source disk and destination path are confirmed.

## After Clean Flash

Before installing `8723du.ko`, check whether USB enumeration is fixed:

```sh
dmesg | grep -Ei 'usb 1-1|error -71|8723|rtl|rtk|wlan'
lsusb
lsusb -t
ip -br link
```

If `lsusb` shows `0bda:d723`, continue with `8723du.ko`.

If `usb 1-1.3 error -71` remains, fix hardware/device-tree/USB power/reset first.
