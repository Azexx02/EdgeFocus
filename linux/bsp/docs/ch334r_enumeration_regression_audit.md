# CH334R Enumeration Regression Audit

## Scope

This note reconstructs the interval between the last confirmed-good CH334R/RTL8723DU USB enumeration and the first confirmed failure where only the EHCI root hub remained visible.

## Last Confirmed Good State

The last clear software log point where the USB Wi-Fi/Bluetooth path still worked is the 2026-07-06 MIPI bring-up check after the earlier v16B audio DTB test:

- Active DTB: `stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v16b-disable-uart7-pd3-sdmode.dtb`.
- The board was reachable by SSH at `192.168.10.171` after reboot.
- `lsmod` included `8723du`, `rtk_btusb`, `bluetooth`, and `cfg80211`.
- `/dev/galcore` was still present.
- This means the CH334R hub and the RTL8723DU module were still enumerating after:
  - NPU/GPU DTB recovery.
  - Multiple MAX98357A/I2S DTB experiments through v16B.
  - UART7 disable / PD3 `AMP_EN` reuse.
  - Initial MIPI v12 panel-driver module install and reboot.

Conclusion: the v9 to v16B DTB/audio work did not by itself break CH334R enumeration.

## First Confirmed Bad State

The first recorded confirmed failure is the 2026-07-08 focus_app/autostart check:

- `/sys/class/net` had only `can0 can1 can2 lo`; no `wlan0`.
- `lsusb` showed only `Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub`.
- `wifi-105-wpa.service` failed because `ip link set wlan0 up` could not find the device.
- Bluetooth was unavailable because no HCI adapter existed.

The important distinction recorded then: this was not a Wi-Fi driver or firmware failure. The CH334R hub itself was missing from USB enumeration.

USB log naming note:

- `hub 1-0:1.0: USB hub found` is the Linux/EHCI root hub and does not prove the external CH334R is present.
- External CH334R presence is indicated by a downstream device on port 1, for example `usb 1-1: new high-speed USB device` followed by `hub 1-1:1.0: USB hub found`.

## 2026-07-08 Detailed Timeline

All times below are local Beijing time from the Codex session record:

- 21:07: user asked whether `focus_app` was set to autostart. SSH to `192.168.10.171` and `192.168.10.173` failed.
- 21:15: user asked to inspect through serial.
- 21:16-21:18: serial inspection showed `focus_app.service` was enabled but inactive because it was waiting for `network-online.target`. The service was changed to depend on `network.target` instead, then it became active.
- 21:18: a separate issue was observed: `wlan0` did not exist and `lsusb` only showed the Linux USB root hub.
- 21:19: user asked to fix `wlan0`.
- 21:20-21:21: serial diagnostics confirmed the EHCI host itself was up, but no downstream CH334R hub was present.
- 21:22: a soft reboot was issued to retry full USB enumeration.
- 21:24: after this reboot, CH334R and RTL8723DU briefly enumerated:
  - kernel time 10.671930: `usb 1-1: new high-speed USB device number 2 using ehci-platform`
  - kernel time 10.829333: `hub 1-1:1.0: USB hub found`
  - kernel time 10.829512: `hub 1-1:1.0: 4 ports detected`
  - kernel time 11.123937: `usb 1-1.1: new high-speed USB device number 3 using ehci-platform`
  - kernel time 11.592615: `rtk_btusb` started probing the Bluetooth function.
  - kernel time 11.618527: `8723du` Wi-Fi module init started.
  - kernel time 12.044579: `usb 1-1: USB disconnect, device number 2`
  - kernel time 12.057412: `usb 1-1.1: USB disconnect, device number 3`
  - kernel time 12.071740 and after: `rtk_btusb` failed with `-71`.
- 21:25: a software hypothesis was tested: avoid simultaneous Wi-Fi/BT firmware initialization by blacklisting/delaying `rtk_btusb` and disabling Realtek USB power saving.
- 21:29-21:32: after applying the mitigation and rebooting, CH334R no longer appeared at all; logs showed only root hub.
- 21:32-21:36: DTB was rolled back from v16B to early selfcarrier v3 to exclude later MIPI/audio DTB changes. v3 still showed only root hub.
- 21:37-21:40: after supply voltage adjustment, EHCI rebind and warm reboot still did not restore CH334R.
- 21:40: user performed a true power-cycle cold boot.
- 21:40-21:41: cold boot still showed no CH334R hub.
- 21:41-21:44: DTB was restored from v3 back to v16B. Final verification still showed only Linux root hub and no `wlan0`.

Interpretation of the 21:24 event: CH334R was not completely dead at that moment. It briefly attached and exposed the RTL8723DU downstream device, then the upstream USB link disconnected about 1.37 seconds after hub attach. Later checks degraded to "CH334R not present at all".

## Transitional Evidence

One reboot near the failure period briefly showed enumeration, then disconnect:

- `usb 1-1: new high-speed USB device number 2 using ehci-platform`
- `hub 1-1:1.0: USB hub found`
- `usb 1-1.1: new high-speed USB device number 3 using ehci-platform`
- Later: `usb 1-1: USB disconnect`
- Wi-Fi firmware download failed.
- Bluetooth patch download failed with `-71`.

This is consistent with an unstable USB attach/power/signal condition. It is not consistent with only missing Realtek firmware, because the hub had to enumerate before Realtek driver loading.

## Operations That Happened After Failure Was Already Observed

These changes were made after CH334R disappearance was already detected, so they are not the original trigger:

- `focus_app.service` dependency change from `network-online.target` to `network.target`.
- RTL8723DU module option mitigation:
  - `rtw_power_mgnt=0`
  - `rtw_enusbss=0`
  - `rtw_btcoex_enable=1`
- Temporary delayed `rtk_btusb` loading.
- Rollback of the above Wi-Fi/BT software mitigations.
- DTB A/B rollback to early selfcarrier v3.
- Restore back to v16B.
- EHCI unbind/rebind tests.
- Suspected power-chip removal.
- CH334R reset attempt.
- CH334R U16 replacement.

The v3 DTB rollback is especially useful evidence: v3 predates the later MIPI/audio DTB edits, but CH334R still did not enumerate after warm restart and user cold power cycle. This makes the later MIPI/audio DTB changes unlikely as the root cause.

## Current Hardware Evidence

Reported board-side checks:

- CH334R 12 MHz crystal is oscillating.
- CH334R local 5 V is present.
- CH334R reset/config-related pin measured 3.3 V.
- U16.13 / C98 local node is normal.
- USBH_D_N/P continuity to CH334R is present.
- CH334R upstream D+/D- have no abnormal short.
- `WIFI_BT_3.3V` is normal.
- `USBH_D_P / U16.11` and `USBH_D_N / U16.10` are both only millivolt-level while Linux is running.
- Replacing U16 did not restore enumeration.
- 2026-07-13: after removing the MAX98357A audio amplifier, serial diagnostics still showed only the Linux root hub:
  - `lsusb`: `Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub`
  - `lsusb -t`: `Driver=ehci-platform/1p`
  - `/sys/class/net`: `can0 can1 can2 lo`
  - dmesg: `hub 1-0:1.0: USB hub found`, with no `usb 1-1` / `hub 1-1` external hub enumeration.
- 2026-07-13: after also removing the RTL8723DU Wi-Fi/Bluetooth module, serial diagnostics still showed only the Linux root hub:
  - `lsusb`: `Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub`
  - `lsusb -t`: `Driver=ehci-platform/1p`
  - `/sys/class/net`: `can0 can1 can2 lo`
  - dmesg: `hub 1-0:1.0: USB hub found`, with no `usb 1-1` / `hub 1-1` external CH334R enumeration.
- 2026-07-13 official-baseboard control test, with the same core board moved back to the original ALIENTEK baseboard and accessed over COM7:
  - `/proc/device-tree/model`: `ALIENTEK STM32MP257 Evaluation Board`
  - kernel: `6.6.48`
  - `/boot/stm32mp257d-atk-ddr-2GB.dtb` hash: `fd1d98259beec067142e8465751a22e828ff4ddf83cf055b8213caab5e4e9310`, same as the v16B DTB restored on the self-developed carrier.
  - `lsusb`:
    - `Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub`
    - `Bus 001 Device 002: ID 1a86:8091 QinHeng Electronics USB HUB`
    - `Bus 001 Device 003: ID 0bda:b733 Realtek Semiconductor Corp. 802.11n WLAN Adapter`
  - `lsusb -t`:
    - root hub `ehci-platform/1p`
    - downstream hub `Driver=hub/4p`
    - Realtek device under hub port 1, with `rtk_btusb` and `rtl8733bu`
  - `/sys/class/net`: `can0 can1 can2 lo wlan0 wlan1`
  - dmesg:
    - kernel time 1.821963: `usb 1-1: new high-speed USB device number 2 using ehci-platform`
    - kernel time 1.983095: `hub 1-1:1.0: USB hub found`
    - kernel time 1.983301: `hub 1-1:1.0: 4 ports detected`
    - kernel time 2.281905: `usb 1-1.1: new high-speed USB device number 3 using ehci-platform`

Interpretation of the official-baseboard control test: the same core board, kernel, EHCI host, and restored v16B DTB can enumerate an external QinHeng USB hub normally on the official baseboard. This makes a pure Linux boot-order / systemd / Wi-Fi-service explanation unlikely for the self-developed carrier's current "only root hub" state. It does not prove the downstream wireless module is identical, because the official board currently shows `0bda:b733` / `rtl8733bu`, while the self-carrier used RTL8723DU earlier, but the hub-level attach path is proven good on the core/software side.
- 2026-07-13: after cleaning the self-developed carrier/core-board connector area and booting again over COM3, with MAX98357A and RTL8723DU still removed, CH334R enumerated again:
  - `lsusb`:
    - `Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub`
    - `Bus 001 Device 002: ID 1a86:8091 QinHeng Electronics USB HUB`
  - `lsusb -t`:
    - root hub `ehci-platform/1p`
    - downstream hub `Driver=hub/4p`
  - dmesg:
    - kernel time 1.820899: `usb 1-1: new high-speed USB device number 2 using ehci-platform`
    - kernel time 1.978442: `hub 1-1:1.0: USB hub found`
    - kernel time 1.978660: `hub 1-1:1.0: 4 ports detected`

Interpretation of the connector-cleaning result: the CH334R hub-level failure is now strongly correlated with the core-board-to-carrier connector / upstream USBH_D_N/P contact path or nearby contamination/contact resistance, not with Linux boot ordering. Because the Wi-Fi/BT module was still removed during this successful hub test, this only proves hub attach recovery; Wi-Fi/BT must be reinstalled and tested separately.

Interpretation: both upstream USB lines staying near 0 V means the MP257 host sees the port as disconnected. CH334R is not asserting upstream attach, or the attach signal is being suppressed/loaded before it reaches the MP257 host.

## Most Likely Regression Window

From the records, the regression window is:

1. After the 2026-07-06 17:10 local-time MIPI v12 reboot check, where SSH at `192.168.10.171` still recovered.
2. Before the 2026-07-08 serial check, where `lsusb` showed only the EHCI root hub.

Within that window, the recorded software/DTB changes do not explain the break because:

- v16B was confirmed good.
- the board was still reachable after the first v12 MIPI panel-module install/reboot on 2026-07-06.
- Later v3 rollback did not restore the hub.
- Wi-Fi/BT mitigations and focus_app service changes happened after the hub was already absent.

The more suspicious category is board-level physical/power work during the broader audio/MIPI/display bring-up period:

- Connector work near audio and MIPI.
- Repeated panel/FFC/power cycling.
- Power rail experiments or supply-voltage changes.
- Any solder/rework heat or mechanical stress near the CH334R upstream USB path, MP257 USBH path, or shared board power.

This is an inference from timing and evidence, not a proven single operation.

## 2026-07-06 / 2026-07-07 Evidence Review

Local artifact timestamps show the following work in this interval:

- 2026-07-06 16:34: screen vendor/reference packages were added under `资料包nanshou_atk_lcd_3p0_and_3p1_480p_资料包`.
- 2026-07-06 17:05-17:11: MIPI v12 panel-driver work was built and tested:
  - `artifacts/kernel/panel-sitronix-st7701-v12-d300-observable-noeot.c`
  - `artifacts/kernel/panel-sitronix-st7701-v12-d300-observable-noeot.ko`
  - `artifacts/dmesg_mipi_v12_observable_noeot.log`
  - `artifacts/mipi_v12_observable_noeot_status.md`
- 2026-07-07 14:27: `自研底板硬件说明（ai生成）.md` was created/updated.

Audio-related records around this interval:

- MAX98357A DTB/software bring-up artifacts were mostly produced on 2026-07-05 evening:
  - v10 through v16B MAX98357A DTB/DTS files.
  - `audio_max_square_1khz_48k_s16_stereo.wav`.
  - v15/v16B pinmux and dmesg notes.
- 2026-07-06 11:39 local time: user said "重新接了，试一下吧" during audio testing.
- 2026-07-06 11:46 local time: user asked for continuous playback to measure voltage from amplifier to speaker.
- 2026-07-06 11:52 local time: user reported measuring `OUTN` and `OUTP` with no voltage.
- 2026-07-06 16:22 local time: user said "刚才修声卡附近的接口的时候，顺带把和mipi的也给一起接上了".

No retained 2026-07-07 self-carrier session record shows additional MAX98357A/audio-board debugging. The visible 2026-07-07 self-carrier record is documentation/report text, and the other 2026-07-07 sessions are unrelated app/icon or `alientek` variant deployment work.

The 2026-07-06 MIPI status note says the board rebooted and entered Linux normally. It also says the runtime DTB was still v16B and no further DTB change was made at that point.

The saved 2026-07-06 dmesg file is a focused MIPI/DSI log. It contains DSI/DRM/panel lines such as `probe`, `prepare`, DSI write return values, and `fb0`, but it does not contain USB, CH334R, RTL8723DU, `wlan0`, or Bluetooth lines.

The 2026-07-06 attached GPT-Pro style MIPI analysis also only discusses MIPI DSI lane count, reset, panel power, backlight, and panel initialization. It does not mention USB, CH334R, Wi-Fi, or Bluetooth.

However, the local Codex conversation record has stronger runtime evidence for 2026-07-06:

- 2026-07-06 11:39 local time: user said "重新接了，试一下吧" during MAX98357A audio testing.
- The board was accessed by SSH at `root@192.168.10.171`. The first SSH attempt timed out, then polling succeeded after about 48 seconds with output `ready`.
- 2026-07-06 11:41 local time: audio playback test over SSH succeeded. Its dmesg tail included `RTW: Turbo EDCA =0x5ea42b`, which means the Realtek Wi-Fi driver was alive at that time.
- 2026-07-06 16:22 local time: user said "刚才修声卡附近的接口的时候，顺带把和mipi的也给一起接上了，试着调一下mipi吧".
- 2026-07-06 16:25 local time: while checking MIPI runtime state over SSH at `192.168.10.171`, `lsmod` output included:
  - `8723du`
  - `rtk_btusb`
  - `bluetooth`
  - `cfg80211`
  This is direct evidence that the RTL8723DU USB Wi-Fi/Bluetooth device was still present and drivers were loaded. Therefore CH334R must still have been enumerating at this point.
- 2026-07-06 17:08 local time: v12 `panel-sitronix-st7701.ko` was installed on the board.
- 2026-07-06 17:10 local time: after rebooting/waiting for the board, SSH at `192.168.10.171` recovered again and MIPI logs were collected. This strongly suggests Wi-Fi was still usable after the v12 panel-module install and reboot.

No local 2026-07-07 board boot log, dmesg, `lsusb`, `ip link`, or Bluetooth status capture was found. The only 2026-07-07 local file found in this workspace is a generated hardware summary document, not a runtime diagnostic log.

The local Codex session records for 2026-07-07 were also checked. They did not contain self-developed-board indicators:

- `192.168.10.171`: 0 matches.
- `developMP257`: 0 matches.
- `CH334R`: 0 matches.
- `lsusb`: 0 matches.
- `wlan0`: 0 matches.
- `hci0`: 0 matches.
- `rtk_btusb`: 0 matches.
- `8723du`: 0 matches.

The visible 2026-07-07 session content was unrelated app/icon work and `alientek` variant deployment work, including target IP `192.168.10.210`, not the self-developed board at `192.168.10.171`.

The board's current `journalctl --list-boots` was checked over COM3 on 2026-07-13. It only retained the current volatile boot, with board RTC time still in 2024 UTC, and did not provide usable 2026-07-06 or 2026-07-07 historical logs.

Conclusion for the earliest-failure question:

- Earliest confirmed-good point is now narrowed later than the previous 2026-07-05 v16B marker.
- 2026-07-06 16:25 local time is a direct confirmed-good point: `8723du`, `rtk_btusb`, and `bluetooth` were loaded while connected over Wi-Fi SSH.
- 2026-07-06 17:10 local time is also strong evidence of Wi-Fi usability after v12 panel-module installation and reboot, because SSH at `192.168.10.171` recovered.
- 2026-07-07 currently has no runtime evidence.
- Earliest confirmed-bad point remains 2026-07-08: `lsusb` only showed the EHCI root hub, with no CH334R hub and no `wlan0`.

So, based on retained evidence, the failure likely occurred sometime after 2026-07-06 17:10 local time and before the 2026-07-08 autostart/serial check. The retained records cannot narrow it further inside that interval.

## Practical Conclusion

## 2026-07-13 Wi-Fi Module Reinstalled Result

After the core-board connector/slot was cleaned, the CH334R hub enumerated again while the RTL8723DU module was still removed. The RTL8723DU Wi-Fi/Bluetooth module was then soldered back and COM3 was checked again.

Observed USB topology:

- `lsusb`:
  - `Bus 001 Device 002: ID 1a86:8091 QinHeng Electronics USB HUB`
  - `Bus 001 Device 003: ID 0bda:d723 Realtek Semiconductor Corp. 802.11n WLAN Adapter`
- `lsusb -t`:
  - root hub `ehci-platform/1p`
  - downstream `Driver=hub/4p`
  - Realtek interface 0/1 bound to `rtk_btusb`
  - Realtek interface 2 bound to `rtl8723du`
- `/sys/class/net` contains `wlan0`.
- `hciconfig -a` shows `hci0` `UP RUNNING PSCAN ISCAN INQUIRY`, Bluetooth name `ATK-DLMP257`, and Realtek manufacturer ID.
- Bluetooth firmware now loads successfully:
  - `rtk_btusb: fw: exists, config file: exists`
  - `rtk_btusb: Rtk patch end 0`
- Wi-Fi firmware download succeeds:
  - `rtl8723d_FirmwareDownload success`

No current log evidence shows the earlier hub-level `usb disconnect` / `error -71` failure after reinstalling the Wi-Fi module. This confirms the hub, the module downstream USB path, and both Realtek drivers are alive again.

Remaining Wi-Fi connection issue is different from the CH334R enumeration failure:

- `wlan0` sees SSID `105`, but signal is weak, around `-80` to `-82 dBm`.
- `wpa_supplicant` can associate with AP `80:af:ca:8c:58:31`, but the WPA group handshake does not complete:
  - `Group handshake still in progress !!!`
  - disconnect reason `7`
- `wlan0` therefore returns to `NO-CARRIER` / `Not connected`, and DHCP has no lease.

Interpretation: the original "hub missing / no wlan0 / no hci0" fault is currently recovered and was most likely a physical connector/contact issue. The current Wi-Fi symptom is a higher-layer association/handshake problem, likely affected by weak RSSI, AP/security settings, or the current Wi-Fi config, not by CH334R enumeration.

After the antenna was connected, Wi-Fi was tested again:

- `lsusb` still showed CH334R and RTL8723DU present.
- `hci0` still stayed up.
- There was still no new `usb disconnect` / `error -71` evidence.
- `wpa_supplicant` completed WPA once, with:
  - `WPA: Key negotiation completed with 80:af:ca:8c:58:31 [PTK=CCMP GTK=CCMP]`
  - `CTRL-EVENT-CONNECTED`
- The connection did not become stable. Later attempts repeatedly associated and then timed out or failed group handshake:
  - `Authentication with 80:af:ca:8c:58:31 timed out`
  - `Group handshake still in progress !!!`
- `iw dev wlan0 link` showed signal still around `-82 dBm`, so the antenna connection did not materially improve the observed RSSI for SSID `105`.
- DHCP still did not obtain an IPv4 lease. `wlan0` briefly had IPv6 addresses from the network, then returned to `DOWN` / `Not connected`.

Updated interpretation: the current remaining Wi-Fi problem should be treated as a radio/AP/security/DHCP issue, not as the earlier CH334R enumeration regression. The next useful isolating test is to place a known-good 2.4 GHz phone hotspot very close to the board and connect to that SSID. If the close hotspot works, the board USB/software path is validated and the `105` AP path is the weak side.

After the antenna was re-soldered, the same test was repeated and the Wi-Fi path recovered:

- CH334R and RTL8723DU still enumerated:
  - `1a86:8091 QinHeng Electronics USB HUB`
  - `0bda:d723 Realtek Semiconductor Corp. 802.11n WLAN Adapter`
- USB topology was correct:
  - `rtk_btusb` bound to Realtek interface 0/1
  - `rtl8723du` bound to Realtek interface 2
- RSSI improved from about `-82 dBm` to about `-66` / `-67 dBm`.
- WPA completed:
  - `WPA: Key negotiation completed with 80:af:ca:8c:58:31 [PTK=CCMP GTK=CCMP]`
  - `CTRL-EVENT-CONNECTED`
- DHCP completed:
  - lease `192.168.10.174` from server `192.168.10.1`
- Runtime state:
  - `wlan0 UP 192.168.10.174/24`
  - `iw dev wlan0 link`: connected to SSID `105`, signal `-67 dBm`, tx bitrate `150.0 MBit/s`
  - `hci0` remained `UP RUNNING`
- Connectivity:
  - `ping 192.168.10.1`: 3/3 received, 0% packet loss
  - `ping 223.5.5.5`: 3/3 received, 0% packet loss

Updated conclusion: the Wi-Fi/BT module stack is now functionally restored. The earlier remaining WPA/DHCP issue was consistent with an antenna/RF path problem. Continue to monitor for long-run stability, but no current evidence points to a USB hub, driver, or DTB regression.

The regression should be treated as a hardware/board-level USB attach failure until proven otherwise.

The highest-value next checks are:

- Compare upstream D+/D- idle voltage at both CH334R pins and MP257-side accessible points.
- Check whether either upstream USB line is loaded low by board leakage, pad damage, solder bridge, or ESD/series component damage.
- Inspect/reflow U16 upstream pins and nearby passives/lands, even though the chip itself has already been replaced.
- If possible, inject or observe a known-good USB FS/HS attach condition on the MP257 USBH upstream path to separate MP257-side damage from CH334R-side attach failure.
- Confirm no board-level power sequencing or reset condition keeps CH334R internally inactive despite nominal static voltages.
