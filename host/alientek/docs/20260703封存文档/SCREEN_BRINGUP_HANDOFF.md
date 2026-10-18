# EdgeFocus Screen Bring-up Handoff

This note summarizes the known-good screen configuration used by the current EdgeFocus board app. It is intended for bringing up the new self-designed baseboard.

## Current Conclusion

- The app uses a userspace SPI LCD path, not a Linux framebuffer device.
- The app opens `/dev/spidev0.0` directly.
- The working DTB is copied from the old system backup:
  - Local backup file: `C:\Users\Aze\Desktop\BoardCode\board_backup_original_system\extracted\boot\stm32mp257d-atk-ddr-2GB.dtb.spidev-pb0cs-copy`
  - Current board active file: `/boot/stm32mp257d-atk-ddr-2GB.dtb`
- On the new SD-card system, only changing `w25q128@0 compatible` from `jedec,spi-nor` to `rohm,dh2228fv` restored `/dev/spidev0.0` but did not light the screen.
- Switching to the old-system `spidev-pb0cs-copy` DTB restored the real screen. Therefore the new baseboard bring-up should compare the complete LCD/SPI/GPIO DTB context, not only the compatible string.

## App-Side LCD Contract

Source: `focus_app/ui/FrameBuffer.cpp`

The app expects:

| Function | Linux node / GPIO line | Default value / behavior |
| --- | --- | --- |
| SPI data | `/dev/spidev0.0` | SPI mode 0, 8 bits |
| SPI chip select | PB0 via SPI controller `cs-gpios` | App does not manually request PB0 |
| LCD power / wake | `PB4` | output high |
| LCD reset | `PB1` | reset sequence: high 20 ms, low 80 ms, high 120 ms |
| LCD D/C | `PF10` | output, command=0 data=1 |
| Backlight | `PD8` | output high by default |

Runtime environment knobs:

| Variable | Default | Meaning |
| --- | --- | --- |
| `EDGEFOCUS_LCD_SPI_SPEED` | `4000000` | SPI max speed in Hz |
| `EDGEFOCUS_LCD_MADCTL` | `0x48` | ILI9341 MADCTL value |
| `EDGEFOCUS_LCD_INVERT_BL` | `0` | `0`: PD8 high means backlight on; `1`: PD8 low means backlight on |

Known tested result:

- `EDGEFOCUS_LCD_INVERT_BL=0` works with the old-system spidev DTB.
- `EDGEFOCUS_LCD_INVERT_BL=1` did not fix the new-system minimal DTB patch case.

## App LCD Init Sequence

The app initializes the panel as ILI9341-compatible:

1. Hardware reset by `PB1`.
2. Command `0x01` software reset.
3. Command `0x3A`, data `0x55` for RGB565.
4. Command `0x36`, data from `EDGEFOCUS_LCD_MADCTL` default `0x48`.
5. Command `0x11` sleep out.
6. Command `0x29` display on.
7. Each flush writes 240 x 320 RGB565 pixels to the full window.

If the new panel is not ILI9341-compatible, the app init sequence must be adjusted. If the panel is the same module, the DTB/GPIO/SPI path is the first suspect.

## Known Good Device Tree Facts

From the old system runtime device tree backup:

- SPI node used for LCD/spidev:
  - `/proc/device-tree/soc@0/bus@42080000/spi@46020000`
  - `compatible = "st,stm32mp25-spi"`
  - `status = "okay"`
  - `reg = <0x46020000 0x400>`
  - `cs-gpios = <... PB0 active-low>`
  - child node name: `w25q128@0`
  - child `compatible = "rohm,dh2228fv"`
  - child `reg = <0>`
  - child `spi-max-frequency = <50000000>`
  - child `status = "okay"`
- SPI8 pinctrl is present:
  - `/proc/device-tree/soc@0/pinctrl@46200000/spi8-0`
  - pins1 pinmux: `0x19204, 0x19004`
  - pins2 pinmux: `0x19104`
  - sleep pinmux: `0x19211, 0x19011, 0x19111`

New-system minimal patch result:

- `/dev/spidev0.0` appeared.
- App log showed SPI LCD initialized.
- Physical screen stayed black.

Old-system complete spidev DTB result:

- `/dev/spidev0.0` appeared.
- App log showed SPI LCD initialized.
- Physical screen lit.

## Verification Commands

Run these on the board:

```sh
ls -l /dev/spidev* /dev/snd/* /dev/video7 2>/dev/null
gpioinfo
rfkill list bluetooth
systemctl is-enabled focus_app.service 2>/dev/null || true
systemctl is-active focus_app.service 2>/dev/null || true
```

Expected important lines:

```text
/dev/spidev0.0
/dev/video7
focus_app.service: disabled / inactive
```

Start the app manually for development:

```sh
rfkill unblock bluetooth
bluetoothctl power on
cd /root
EDGEFOCUS_VISION_MODE=real EDGEFOCUS_LCD_INVERT_BL=0 nohup ./focus_app --log-level debug > /tmp/focus_app_lcd.log 2>&1 &
```

Check LCD initialization:

```sh
grep -E "FrameBuffer|UIManager|spidev|HealthMonitor|BleGatt|BleBand" /tmp/focus_app_lcd.log
```

Known good log lines:

```text
[FrameBuffer] SPI LCD config speed=4000000Hz madctl=0x48 backlight=active-high
[FrameBuffer] Initialized userspace SPI LCD driver: 240x320 on /dev/spidev0.0
[UIManager] FrameBuffer: 240x320 (real=no)
[BleGattServer] BlueZ advertisement registered: name=EdgeFocus
[BleBandController] Connected to BLE band target ...
[HealthMonitor] focus_app is running - health OK
```

`real=no` is not a failure in this app. It only means the old `/dev/fb0` mmap path is not used; the userspace SPI LCD path can still be working.

## Files To Preserve

Local:

```text
C:\Users\Aze\Desktop\BoardCode\board_backup_original_system\orig_boot.tgz
C:\Users\Aze\Desktop\BoardCode\board_backup_original_system\orig_proc_device_tree_full.tgz
C:\Users\Aze\Desktop\BoardCode\board_backup_original_system\orig_system_snapshot.txt
C:\Users\Aze\Desktop\BoardCode\board_backup_original_system\extracted\boot\stm32mp257d-atk-ddr-2GB.dtb.spidev-pb0cs-copy
```

Board:

```text
/boot/stm32mp257d-atk-ddr-2GB.dtb
/boot/stm32mp257d-atk-ddr-2GB.dtb.original-spidev-abtest
/boot/stm32mp257d-atk-ddr-2GB.dtb.bak-before-original-spidev-abtest-*
```

## Bring-up Checklist For New Baseboard

1. Confirm the LCD module controller is ILI9341-compatible, or update the app init sequence.
2. Confirm SPI bus maps to `/dev/spidev0.0`; if not, either fix DTB or update the app path.
3. Confirm SPI pins and chip select for SPI8:
   - SPI node equivalent to `spi@46020000`.
   - CS0 on PB0 through `cs-gpios`.
4. Confirm GPIO line names exposed by `gpioinfo`:
   - `PB4`, `PB1`, `PF10`, `PD8`.
5. Confirm GPIO wiring:
   - PB4: LCD power/wake.
   - PB1: LCD reset.
   - PF10: LCD D/C.
   - PD8: backlight.
6. Start with `EDGEFOCUS_LCD_SPI_SPEED=4000000`.
7. Test backlight polarity with `EDGEFOCUS_LCD_INVERT_BL=0` first; try `1` only if all DTB/GPIO lines are confirmed.
8. Treat `/dev/spidev0.0 exists but screen black` as insufficient; use physical screen light plus app log as the acceptance criteria.
