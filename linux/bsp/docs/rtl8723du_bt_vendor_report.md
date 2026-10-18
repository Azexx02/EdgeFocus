# RTL8723DU Bluetooth issue report

## Board / system

- SoC board: STM32MP257 / ALIENTEK STM32MP257 Evaluation Board based system
- Kernel: `Linux 6.6.48 #1 SMP PREEMPT Tue May 6 18:09:52 CST 2025 aarch64`
- USB device: `0bda:d723 Realtek Semiconductor Corp. 802.11n WLAN Adapter`
- Wi-Fi side: works with `8723du` driver, connected as `wlan0`
- Bluetooth driver:

```text
filename: /lib/modules/6.6.48/kernel/drivers/bluetooth/bluetooth_usb_driver/rtk_btusb.ko
version: 3.1.0a23ffd.20230801-094611
description: Realtek Bluetooth USB driver ver 3.1.0a23ffd.20230801-094611
depends: bluetooth
vermagic: 6.6.48 SMP preempt mod_unload modversions aarch64
```

## Requested help

Please provide the correct Linux Bluetooth firmware/config files for this RTL8723DU USB module and `rtk_btusb` driver:

```text
/lib/firmware/rtl8723du_fw
/lib/firmware/rtl8723du_config
```

Please also confirm whether the current `rtk_btusb` driver version is suitable for Linux 6.6.48 / BlueZ, or whether a different Realtek Bluetooth driver package is required.

## Original failure before adding firmware

The board originally did not contain the files requested by `rtk_btusb`:

```text
rtk_btusb: config filename rtl8723du_config
usb 1-1.1: Direct firmware load for rtl8723du_config failed with error -2
rtk_btusb: fw name is  rtl8723du_fw
usb 1-1.1: Direct firmware load for rtl8723du_fw failed with error -2
rtk_btusb: request_firmware error
rtk_btusb: load firmware failed!
rtk_btusb: btusb_open failed
```

## Current test result after adding generic firmware

I tested a generic upstream `rtl8723d_fw.bin` renamed to:

```text
/lib/firmware/rtl8723du_fw
```

I also copied the board image's existing 14-byte Realtek config file to:

```text
/lib/firmware/rtl8723du_config
```

Current files:

```text
-rwxr--r-- 1 root root    14 /lib/firmware/rtl8723du_config
-rw-r--r-- 1 root root 52080 /lib/firmware/rtl8723du_fw
```

The firmware patch is now accepted and downloaded successfully:

```text
rtk_btusb: config filename rtl8723du_config
rtk_btusb: fw name is  rtl8723du_fw
rtk_btusb: This is not 8723a, use new patch style!
rtk_btusb: rtk_get_fw_project_id: opcode 0, len 1, data 9
rtk_btusb: lmp_version is 8723, project_id is 8723, match!
rtk_btusb: fw_version = 0x828a96f1
rtk_btusb: Coexistence: BTCOEX_20190509-2f2f
rtk_btusb: fw: exists, config file: exists
rtk_btusb: load_firmware done
rtk_btusb: download_data done
rtk_btusb: read_ver_rsp->lmp_subver = 0x96f1
rtk_btusb: read_ver_rsp->hci_rev = 0x828a
rtk_btusb: Rtk patch end 0
rtk_btusb: btusb_open set HCI UP RUNNING
```

But Bluetooth still cannot be used:

```text
Bluetooth: hci0: Opcode 0x0c77 failed: -56
rtk_btusb: btusb_close
```

`hciconfig -a`:

```text
hci0: Type: Primary  Bus: USB
      BD Address: 00:E0:4C:23:99:87
      DOWN
      ACL MTU: 1021:8  SCO MTU: 255:12
```

`bluetoothctl show`:

```text
No default controller available
```

## btmon detail

The failing HCI command is:

```text
< HCI Command: Read Synchronization Train Parameters (0x03|0x0077)
> HCI Event: Command Complete
  Read Synchronization Train Parameters (0x03|0x0077)
    invalid packet size
    01 08 00 00 00 7c
= Close Index: 00:E0:4C:23:99:87 [hci0]
```

## Note about config tests

I briefly tested an 18-byte experimental config based on the existing 14-byte config to see whether the HCI feature bits could be adjusted. It did not fix the `0x0c77` failure and has been reverted.

The board currently has the 14-byte config copied from the original image:

```text
55 ab 23 87 08 00 8d 00 01 fa 8f 00 01 bf
```

## Question for module vendor

Could you provide the correct `rtl8723du_fw` and `rtl8723du_config` files for this module, or the complete Realtek Bluetooth Linux driver/firmware package for VID/PID `0BDA:D723`?

