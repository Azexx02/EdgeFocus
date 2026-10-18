# Self-Carrier SPI Screen Bring-up Status

Date: 2026-07-01

## Board And System

- Board: self-developed carrier with STM32MP257 core board.
- Current access:
  - SSH over Wi-Fi: `root@192.168.10.173`
  - Serial console: COM16, 115200 baud.
- Current base DTB on board: `/boot/stm32mp257d-atk-ddr-2GB.dtb`
- Current DTB lineage:
  - clean original ATK DTB
  - self-carrier boot cleanup DTB
  - audio v5.4 DTB
  - screen v6 DTB enabling SPI8 spidev on PB0 CS.
- Current working system state:
  - Wi-Fi works and SSH is available after reboot.
  - Bluetooth works with Radxa RTL8723DU firmware.
  - Systemd failed units were cleaned up for the self-carrier hardware.
  - Audio MAX98357A is recorded as unresolved and currently paused.

## Screen Hardware From Self-Carrier Netlist

Self-carrier netlist file:

`D:\Aze\Documents\developMP257\hardware\V2(3).tel`

Observed LCD-related nets:

| Function | Netlist net | Connector pin | Core-board pin/net |
| --- | --- | --- | --- |
| LCD power | `5V_SYS` | `LCD屏幕.1` | board 5 V |
| LCD GND | GND | `LCD屏幕.2`, `LCD屏幕.15`, `LCD屏幕.16` | GND |
| LCD CS | `PB0` | `LCD屏幕.3` | `U1.D3` |
| LCD reset | `MIPI_RESET` | `LCD屏幕.4` | `U1.D4` |
| LCD DC/RS | `PF10` | `LCD屏幕.5` | `U1.D6` |
| LCD MOSI | `PZ0` | `LCD屏幕.6` | `U1.B73` |
| LCD SCK | `PZ2` | `LCD屏幕.7` | `U1.B71` |
| LCD backlight | `LCD_BL` | `LCD屏幕.8` | `U1.B39` |
| LCD ID | `LCD_ID` | `LCD屏幕.9` | `U1.B72` |
| Touch SDA | `CT_SDA` | `LCD屏幕.10` | `U1.A45` |
| Touch reset | `CT_RESET` | `LCD屏幕.11` | `U1.D8` |
| Touch SCL | `CT_SCL` | `LCD屏幕.12` | `U1.A46` |
| Touch INT | `CT_INT` | `LCD屏幕.13` | `U1.D9` |

Important mismatch to keep in mind:

- Old software/docs assume LCD reset is `PB1`.
- Self-carrier netlist names LCD reset as `MIPI_RESET` on `U1.D4`.
- Old software/docs assume LCD backlight is `PD8`.
- Self-carrier netlist names LCD backlight as `LCD_BL` on `U1.B39`.
- Need confirm from the core-board pin table or hardware measurement whether `U1.D4 == PB1` and `U1.B39 == PD8`.

## Reference Old Development System

User provided old development system handoff:

`C:\Users\Aze\Desktop\BoardCode\docs\SCREEN_BRINGUP_HANDOFF.md`

Key facts from the old system:

- The app uses userspace SPI LCD, not framebuffer/DRM.
- The app opens `/dev/spidev0.0`.
- SPI mode 0, 8 bits, default 4 MHz.
- Old app control assumptions:
  - PB0: hardware CS through SPI controller `cs-gpios`
  - PB4: LCD power/wake
  - PB1: LCD reset
  - PF10: LCD D/C
  - PD8: backlight
- Old app uses a short ILI9341-compatible init sequence:
  - reset
  - `0x01`
  - `0x3A = 0x55`
  - `0x36 = 0x48`
  - `0x11`
  - `0x29`

Old known-good DTB:

`C:\Users\Aze\Desktop\BoardCode\board_backup_original_system\extracted\boot\stm32mp257d-atk-ddr-2GB.dtb.spidev-pb0cs-copy`

Workspace copy:

`D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\old-system-spidev-pb0cs-copy.dtb`

## Three-DTB Comparison Result

Compared:

1. Old development-system DTB that could light the screen.
2. Clean original-system DTB.
3. Current self-carrier v6 DTB.

Result:

- Old development-system DTB vs clean original-system DTB has only two differences:
  - `spi@46020000/cs-gpios`: changed from GPIOZ3 to PB0 active-low.
  - `spi@46020000/w25q128@0 compatible`: changed from `jedec,spi-nor` to `rohm,dh2228fv`.
- Current self-carrier v6 already includes both differences.
- Current self-carrier v6 has the same `spi@46020000`, `spi8-0`, and `spi8-sleep-0` content as the old development-system screen DTB.

Implication:

- The screen failure is unlikely to be caused by missing extra SPI8 DTB context from the old development system.
- The next likely area is physical/control-line mapping: backlight, reset, panel power, cable orientation, or exact LCD controller/init.

## Current DTB Screen V6

Workspace files:

- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v6-spi8-spidev-pb0cs.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v6-spi8-spidev-pb0cs.dtb`

Board backup before v6:

`/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v5.4-before-v6-spi8-screen-20240228_091308.bak`

Screen v6 changes:

- `spi@46020000 status = "okay"`
- `cs-gpios = <GPIOB 0 active-low>`
- `w25q128@0 compatible = "rohm,dh2228fv"`
- `w25q128@0 status = "okay"`

Runtime result:

- `/dev/spidev0.0` appears.
- `spi_stm32 46020000.spi: driver initialized (master mode)` appears.
- PB0 is claimed by SPI CS.
- PZ0/PZ1/PZ2 are claimed by SPI8 when SPI8 is bound.

## Test Program

Workspace source:

`D:\Aze\Documents\developMP257\tools\ili9341_spidev_test.c`

Board binary:

`/root/ili9341_spidev_test`

Latest board binary SHA256:

`40fd24aeec2a62dca1b6337e91c7296763eb0b26710d8346092c08ef995f173f`

Features:

- Hardware spidev tests:
  - `byte00`
  - `byte3a`
  - `init`
  - `fill`
- GPIO bit-bang tests:
  - `bb-reset`
  - `bb-init`
  - `bb-fill`
  - `bb-full-fill`
  - `bb-st7789-fill`

GPIO assumptions in the test program:

- PB0: CS
- PB1: LCD reset
- PB4: LCD power/wake
- PF10: DC
- PD8: backlight
- PZ0: MOSI
- PZ2: SCK

## Test Results

### Hardware SPI8

`/dev/spidev0.0` exists, but hardware SPI transfer is not reliable.

At 1 MHz:

- Byte `0x00`: succeeds.
- Byte `0x3a`: fails with kernel timeout:

```text
spidev spi0.0: SPI transfer timed out
spi_master spi0: failed to transfer one message from queue
spi_master spi0: noqueue transfer failed
```

Implication:

- The SPI8 controller path has an unresolved transfer problem.
- This is independent of the full main application.

### GPIO Bit-Bang SPI

SPI8 was temporarily unbound to free PZ0/PZ2/PB0.

The following tests completed without software errors:

- Short ILI9341-like init + RGB fill: `bb-fill`
- Full old example ILI9341 init + RGB fill: `bb-full-fill`
- ST7789 init + RGB fill: `bb-st7789-fill`

User visual result:

- Screen still did not light.

Implication:

- Since even GPIO bit-bang completes but screen stays dark, the next priority is not the SPI controller timeout.
- Focus on whether the LCD is actually powered, reset released, backlight enabled, and connected with the correct pin mapping.

### Backlight Candidate Test

Ran a direct PD8 toggle test:

```sh
gpioset --banner --toggle 5s,5s,5s,5s,0 PD8=1
```

## MIPI DSI Bring-up V7

Date: 2026-07-02

User-provided MIPI pin mapping:

- `DSI_RESET -> PB1`
- `DSI_BL -> PD8`
- `LCD_ID -> PZ1`
- Touch later: `CTP_SCL -> PZ4/I2C8_SCL`, `CTP_SDA -> PZ9/I2C8_SDA`, `CTP_RST -> PB4`, `CTP_INT -> PB5`
- Schematic image `AFC01-S26FCA-00` shows the display connector routes only `MIPI_DP0/DN0`, `MIPI_DP1/DN1`, and `MIPI_CLKP/CLKN`. DSI lanes 2 and 3 are not routed, so the self-carrier MIPI screen must be treated as a 2-lane DSI panel, not the 4-lane official-board DTB default.

Workspace files:

- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7-mipi-dsi-gpio-bl.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7-mipi-dsi-gpio-bl.dtb`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.1-mipi-dsi-id0.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.1-mipi-dsi-id0.dtb`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.2-mipi-dsi-2lane-id0.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.2-mipi-dsi-2lane-id0.dtb`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.3-mipi-dsi-2lane-id1.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.3-mipi-dsi-2lane-id1.dtb`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.4-mipi-dsi-2lane-id2.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.4-mipi-dsi-2lane-id2.dtb`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.5-mipi-dsi-2lane-id3.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v7.5-mipi-dsi-2lane-id3.dtb`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v8-st7701-d300t9307v1.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v8-st7701-d300t9307v1.dtb`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v8.1-st7701-d300t9307v1-reset-active-high.dts`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v8.1-st7701-d300t9307v1-reset-active-high.dtb`
- `D:\Aze\Documents\developMP257\artifacts\kernel\panel-sitronix-st7701.c`
- `D:\Aze\Documents\developMP257\artifacts\kernel\panel-sitronix-st7701-d300t9307v1.ko`
- `D:\Aze\Documents\developMP257\artifacts\kernel\panel-sitronix-st7701-v8.2-video-lpm.c`
- `D:\Aze\Documents\developMP257\artifacts\kernel\panel-sitronix-st7701-v8.2-video-lpm.ko`
- `D:\Aze\Documents\developMP257\scripts\fb_color_bars.py`

Board backup before v7:

- `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v6-before-v7-mipi-.bak`

V7 changes:

- Enabled `display-controller@48010000` LTDC.
- Enabled `dsi@48000000`.
- Added DSI panel node using `compatible = "alientek,mipi-lcd"`, four DSI lanes, reset on `PB1`, panel power from `vdd_5v`.
- Added `panel-dsi-backlight` using `gpio-backlight` on `PD8`.
- Added `dsi_lcd_id { dsi_select_id = <0x02>; }` from the official MIPI DTB.
- Disabled `spi@46020000` and its `w25q128@0` child for this MIPI test because SPI8 previously claimed PZ1 while the MIPI connector uses `LCD_ID -> PZ1`.

Runtime result after reboot:

- Board boots and SSH returns on `192.168.10.173`.
- `/dev/dri/card0` exists.
- `/dev/fb0` exists and reports `stmdrmfb`, `720,1280`, `16` bpp.
- `/sys/class/backlight/panel-dsi-backlight` exists with `max_brightness=1`.
- `modetest -M stm -c -e` reports `DSI-1 connected`, preferred mode `720x1280` at about `55.8 Hz`, `DPMS=On`, `link-status=Good`.
- Display dmesg shows DRM initialized and `fb0` created; no DSI/panel/backlight probe failure was seen.
- Initial `/tmp/fb_color_bars.py` wrote each framebuffer line as `720 * 2 = 1440` bytes, but the actual `fb0` stride is `1536` bytes. This could produce a gray or corrupted image because each line starts at the wrong offset.
- Updated `D:\Aze\Documents\developMP257\scripts\fb_color_bars.py` to read `/sys/class/graphics/fb0/stride` and pad each RGB565 line to the real stride.
- Re-ran `/tmp/fb_color_bars.py` after setting `fb0 blank=0`, `panel-dsi-backlight/bl_power=0`, and `brightness=1`; user visual confirmation is still needed.

V7.1 test:

- Changed only `dsi_lcd_id.dsi_select_id` from `<0x02>` to `<0x00>`.
- Board boots; DSI remains connected; framebuffer and backlight remain present.
- User reported the screen was still gray.

V7.2 test:

- Based on v7.1, changed only panel `dsi-lanes` from `<0x04>` to `<0x02>` to match the schematic's two routed DSI data lanes.
- Board backup before v7.2: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v7.1-before-v7.2-mipi-2lane.bak`
- Board boots; DSI remains connected; framebuffer reports `720,1280`, `16` bpp, stride `1536`.
- GPIO debug shows `PB1 reset` released (`out hi ACTIVE LOW`) and `PD8 panel-dsi-backlight` high.
- Re-ran the stride-aware color bar script; user visual confirmation is pending.

V7.3 test:

- Based on v7.2, changed only `dsi_lcd_id.dsi_select_id` from `<0x00>` to `<0x01>`.
- Board backup before v7.3: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v7.2-before-v7.3-mipi-2lane-id1.bak`
- Board boots; DSI remains connected; framebuffer remains `720,1280`, `16` bpp, stride `1536`.
- Re-ran the stride-aware color bar script; user visual confirmation is pending.

V7.4 test:

- Based on v7.2, changed only `dsi_lcd_id.dsi_select_id` to `<0x02>` while keeping `dsi-lanes = <0x02>`.
- User-provided product photo identifies the screen as a 3.0 inch `480x854` MIPI/RGB panel, not the official 720x1280 panel.
- Board backup before v7.4: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v7.3-before-v7.4-mipi-2lane-id2.bak`
- Board boots; DSI remains connected, but the exported DRM/fb mode is still `720x1280`, not `480x854`.
- This indicates `dsi_select_id = 2` is still selecting an official 720x1280 mode and does not match the attached 480x854 panel.

V7.5 test:

- Based on v7.2, changed only `dsi_lcd_id.dsi_select_id` to `<0x03>` while keeping `dsi-lanes = <0x02>`.
- Board backup before v7.5: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v7.4-before-v7.5-mipi-2lane-id3.bak`
- Board boots; DSI remains connected, but the exported DRM/fb mode changed to `1080x1920`, not `480x854`.
- Conclusion from v7.1 through v7.5: the available official `dsi_select_id` values tested so far do not select the user's 3.0 inch `480x854` panel. The current attached screen likely needs its own panel timing and DSI initialization sequence, not just DTB GPIO/lane edits.

Rollback after v7.5:

- Restored board `/boot/stm32mp257d-atk-ddr-2GB.dtb` to v7.2 (`dsi-lanes = <0x02>`, `dsi_select_id = <0x00>`) to avoid leaving the board on the incorrect v7.5 `1080x1920` mode.
- Board backup before rollback: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v7.5-before-rollback-v7.2.bak`
- After reboot, DSI is connected, framebuffer is back to `720,1280`, `16` bpp, stride `1536`, PB1 reset is released, and PD8 backlight is high.
- This is not the final correct panel configuration; it is only a safer temporary state while waiting for the 480x854 panel IC/timing/init data.

Needed next from panel vendor or product page:

- LCD driver IC model, for example ST7701/ILI9881/NT355xx/HX839x/etc.
- MIPI DSI lane count and expected bit rate or pixel clock.
- Full 480x854 timing: hsync/hbp/hfp/vsync/vbp/vfp and refresh rate.
- DSI init command table, including sleep-out/display-on delays.
- Whether the module expects RGB565 or RGB888 over DSI.

## ST7701S D300T9307V1 V8

Date: 2026-07-02

Reference:

- User-provided open hardware page: `https://oshwhub.com/nanshou/atk_mipi_lcd_module_3_and_3-1_inch`
- The page identifies the 3.0 inch panel as `D300T9307V1`, `480x854`, `ST7701S`, MIPI video mode, RGB + 3-line SPI capable.
- The page provides the 3.0 inch init sequence and timing:
  - Pixel clock: `27000000`
  - Active: `480x854`
  - HFP/HSYNC/HBP: `8/2/43`
  - VFP/VSYNC/VBP: `4/10/42`
  - Init command sequence uses ST7701S command pages and ends with `0x36=0x00`, `0x3A=0x77`, sleep out, display on.

Kernel/module work:

- The kernel tree already contained `drivers/gpu/drm/panel/panel-sitronix-st7701.c`, but `.config` had `# CONFIG_DRM_PANEL_SITRONIX_ST7701 is not set`.
- Enabled it as a module on the cross host and added a new compatible:
  - `nanshou,d300t9307v1`
- Added a D300T9307V1 panel descriptor using the open hardware page's `480x854` timing and init command sequence.
- Built module:
  - Board path: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko`
  - Local copy: `D:\Aze\Documents\developMP257\artifacts\kernel\panel-sitronix-st7701-d300t9307v1.ko`
  - `modinfo` alias includes `of:N*T*Cnanshou,d300t9307v1`.

DTB v8 changes:

- Based on v7.2.
- `panel@0 compatible` changed from `alientek,mipi-lcd` to `nanshou,d300t9307v1`, `sitronix,st7701`.
- Panel binding changed to ST7701 properties:
  - `reset-gpios = <GPIOB 1 GPIO_ACTIVE_LOW>`
  - `VCC-supply = <vdd_5v>`
  - `IOVCC-supply = <vdd_3v3>`
  - `backlight = <panel-dsi-backlight>`
- SPI8 remains disabled to avoid the `LCD_ID=PZ1` conflict.

Board install:

- Installed module to `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko` and ran `depmod -a`.
- Installed v8 over `/boot/stm32mp257d-atk-ddr-2GB.dtb`.
- Board backup before v8: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v7.2-before-v8-st7701-d300t9307v1.bak`

Runtime result:

- Board boots and SSH returns.
- `lsmod` shows `panel_sitronix_st7701`.
- `fb0` is now `480,854`, `16` bpp, stride `1024`.
- `modetest -M stm -c -e` reports `DSI-1 connected`, physical size `37x65`, preferred mode `480x854`.
- Display dmesg shows ST7701 module loaded and DRM/fb0 created; no ST7701 probe failure was seen.
- `scripts\fb_color_bars.py` was updated to read `fb0` width/height/stride dynamically and has been rerun on the board.
- User visual confirmation is pending.

V8.1 reset polarity fix:

- User reported v8 was still gray.
- Checked `/sys/kernel/debug/gpio` and found `PB1 reset` was `out lo ACTIVE LOW`, meaning the ST7701 driver left the panel reset line physically low.
- Root cause: `panel-sitronix-st7701.c` toggles reset as logical low then logical high; with an active-low GPIO descriptor this results in physical high then physical low. The panel can remain held in reset during initialization.
- Created v8.1 by changing only:
  - `reset-gpios = <GPIOB 1 GPIO_ACTIVE_LOW>` to `reset-gpios = <GPIOB 1 GPIO_ACTIVE_HIGH>`
- Board backup before v8.1: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v8-before-v8.1-st7701-reset-active-high.bak`
- After reboot, `PB1 reset` is `out hi`, `PD8 panel-dsi-backlight` is `out hi`, `fb0` remains `480,854`, stride `1024`, and `DSI-1` remains connected at `480x854`.
- Re-ran the dynamic color bar script; user visual confirmation is pending.

V8.2 DSI mode flag experiment:

- User reported v8.1 was still gray.
- Created a module-only experiment; DTB stayed on v8.1.
- Changed only `panel-sitronix-st7701.c` DSI flags from:
  - `MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS`
  - to `MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM`
- Installed v8.2 module over `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko`.
- Board backup before v8.2 module: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.1-reset-active-high.bak`
- After reboot, `panel_sitronix_st7701` is loaded, `fb0` remains `480,854`, stride `1024`, `DSI-1` remains connected at `480x854`, PB1 reset is high, and PD8 backlight is high.
- Re-ran the dynamic color bar script; user visual confirmation is pending.

Important temporary side effect:

- While v7 is installed, the old SPI screen path `/dev/spidev0.0` is intentionally disabled. Re-enable SPI8 only after finishing the MIPI test or after deciding to return to the SPI display path.

User result:

- Screen still did not light.

Implication:

- Either PD8 is not the actual self-carrier backlight line, or the backlight power/path/polarity/cable is not correct.
- This aligns with the netlist concern: self-carrier has `LCD_BL -> U1.B39`, while old software assumes `PD8`.

## Current Hypotheses

Most likely:

1. `LCD_BL=U1.B39` is not Linux `PD8`, or the backlight circuit is not being driven as expected.
2. `MIPI_RESET=U1.D4` is not Linux `PB1`, so LCD reset may never be toggled correctly.
3. Screen power/backlight supply or FPC orientation is wrong.
4. The panel controller/init differs from ILI9341/ST7789, but this is lower priority while backlight remains completely dark.

Less likely:

1. Missing old development-system DTB screen context. The three-DTB comparison does not support this.
2. Full main application dependency failures. The standalone tests bypass the app and still do not light the screen.

## Recommended Next Steps

1. Get or derive the core-board connector pin table mapping `U1.B39` and `U1.D4` to SoC GPIO names.
2. Measure voltage while toggling:
   - `LCD_BL` at LCD connector pin 8.
   - `MIPI_RESET` at LCD connector pin 4.
   - LCD pin 1 `5V_SYS`.
3. Confirm FPC orientation and whether LCD pin numbering matches the schematic symbol.
4. If `U1.B39` and `U1.D4` map to different GPIOs, update the standalone test program first, not the main app.
5. After backlight/reset are confirmed, return to SPI:
   - Use GPIO bit-bang first for proof of panel life.
   - Then debug hardware SPI8 timeout separately.

## Do Not Forget

- SPI8 is sometimes manually unbound during bit-bang tests. Restore it with:

```sh
echo 46020000.spi > /sys/bus/platform/drivers/spi_stm32/bind
```

- Current state after the last test:
  - SPI8 was rebound.
  - `/dev/spidev0.0` exists again.

## MIPI DSI Current State

Current tested panel:

- `D300T9307V1`, 3.0 inch, `480x854`.
- LCD driver IC: `ST7701S`.
- Interface: 2-lane MIPI DSI video mode on the self-carrier schematic.
- GPIOs currently used:
  - `DSI_RESET = PB1`, installed as active-high for the ST7701 driver.
  - `DSI_BL = PD8`, GPIO backlight high after boot.
  - `LCD_ID = PZ1`, SPI8 path disabled while MIPI is under test.

Current installed software state:

- DTB: rolled back to self-carrier v3 baseline after v8.8 still showed gray screen.
- Panel module: v8.8 ATK RK3506 reference sequence/timing remains on disk at `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko`, but v3 DTB has no active ST7701 panel node, so it is not part of the active boot path.
- Module backups:
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.7-before-v8.8-atk-rk3506.bak`
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.5-before-v8.7-burst.bak`
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.5-vendor-only.bak`
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.1-reset-active-high.bak`
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.2-video-lpm.bak`
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.3-log-writes.bak`
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.4-wrong-format.bak`
- Local module source/artifact:
  - `artifacts/kernel/panel-sitronix-st7701-v8.8-d300-atk-rk3506.c`
  - `artifacts/kernel/panel-sitronix-st7701-v8.8-d300-atk-rk3506.ko`

Observed software-side result:

- `panel_sitronix_st7701` loads.
- v8.8 SHA256: `96610ed31caef79cd61d9311cd846dbe097325b3b4fcf079d1065eacc8b9cedb`.
- v8.7 rollback backup SHA256: `5359f7405a20e4dd434ded487220a33b95f2040fb0743a2c5df22ea9b0aeccf6`.
- v8.5 restore/rollback backup SHA256: `275824a34be068c00b21e7c33cc6f0585739a0bc73c5d5ef81780009950712a5`.
- Active DTB after rollback: `/boot/stm32mp257d-atk-ddr-2GB.dtb`, SHA256 `ffbf5d70d924dfeb459026bfcf837773c62d71ae9b38952401a0c2c30d6020f5`, matching local `clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v3.dtb`.
- Board DTB backup before rollback: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v8.8-before-rollback-v3-20260702.bak`, SHA256 `e182fb90e403cc1565b658cfe44d9a9f9bcd9c6525f36e27489e7aa3a9811f9f`.
- Normal boot reaches root shell and SSH; `systemctl --failed` reports 0 failed units.
- Network baseline after rollback: `/sys/class/net` contains `can0 can1 can2 lo wlan0`, and `wlan0` is up at `192.168.10.173/24`.
- Because the active DTB is v3, there is no active MIPI framebuffer path from the v8.x ST7701 experiments.
- v8.8 logs `prepare: lanes=2 format=0 mode_flags=0x803`, meaning RGB888, 2 DSI lanes, video burst, LPM.
- v8.8 uses the author's ATK RK3506 reference differences: initial soft reset + 120 ms, `C1 12 0C`, final `3A 70`, and timing `hfp/hsync/hbp=7/2/11`, `vfp/vsync/vbp=22/2/12`.
- No `DSI write failed` messages were seen in the ST7701 init path before the v8.6 readback experiment.
- The dynamic framebuffer color bar script runs without error.
 - On v8.7, RGB565 color bars were written directly to `/dev/fb0` via serial Python because the temporary `/tmp/fb_color_bars.py` script was absent.

Visual result:

- User reported gray screen through v8.8. Active DTB is now rolled back to v3 before starting a cleaner MIPI-only v9 attempt.

Recovery note:

- v8.6 DCS-readback diagnostic caused a watchdog reset loop shortly after `panel_sitronix_st7701`/DRM init. Recovery was done by temporarily booting with `modprobe.blacklist=panel_sitronix_st7701` and restoring the v8.5 module backup. Do not reinstall v8.6 unless the watchdog/readback risk is controlled.

## MIPI DSI V9 Clean Attempt

Reason for v9:

- User asked to return to a clean baseline and retry according to the author's screen material.
- Active DTB was rolled back to self-carrier v3 first, so v9 does not inherit the earlier SPI-screen or audio experiments.

Self-carrier pin mapping used in v9:

- `DSI_RESET = PB1`
- `DSI_BL = PD8`
- `LCD_ID = PZ1`
- Touch, not enabled yet: `CTP_SCL=PZ4/I2C8_SCL`, `CTP_SDA=PZ9/I2C8_SDA`, `CTP_RST=PB4`, `CTP_INT=PB5`
- The latest user reference image shows DSI clock plus D0/D1/D2/D3 routed to the MP257 dedicated DSI lanes, so the current hypothesis is 4-lane DSI.

Current installed DTB:

- Local artifact: `clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v9-mipi-d300-minimal.dtb`
- SHA256: `6f1bebc6d16b0a8bbfeeeffdadc8bff20240ef41d9f78a9a79d8e159e699682a`
- Board backup before install: `/boot/stm32mp257d-atk-ddr-2GB.dtb.selfcarrier-v3-before-v9-mipi-d300-20260702.bak`
- Only LTDC/DSI/ST7701 panel/backlight are added on top of v3.
- SPI8 remains disabled so `PZ1/LCD_ID` is not occupied.

Current installed panel module:

- Corrected v9.1 4-lane module: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko`
- Local artifact: `artifacts\kernel\panel-sitronix-st7701-v9.1-d300-4lane.ko`
- SHA256: `b982772849c429bfce69849b2a0de2ddc535408062edda03cd447ed76905ccc0`
- Backup of previous mistaken v9.1 module: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v9.1-mistaken-before-correct-4lane-20260702.bak`
- Backup SHA256: `17a26a8c913f2cd5d7af1357080f4c2da0c132a4ce38677aa332f59ad34901c3`
- Backup of v9 exact-sequence module before v9.1: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v9-before-v9.1-4lane-20260702.bak`
- V9 SHA256: `98da7ce8a937babf0fca063ccaa1b816f95a3c34f9f4f55bbdfad78a9aff8ed6`

Verified software result after corrected v9.1:

- Kernel log: `prepare: lanes=4 format=0 mode_flags=0x803`
- Kernel log: `prepare: using vendor sequence only`
- Kernel log: `enable display on`
- `fb0` exists as `480x854`, `16bpp`, stride `1024`
- `PB1 reset` is `out hi`
- `PD8 panel-dsi-backlight` is `out hi`
- `PZ1` is unclaimed/free
- `tools\fb_bars_runtime.py` was copied to `/tmp` and ran successfully:
  - `FB_BARS_WRITTEN 480 854 1024 16`

Current visual result:

- User visual confirmation after corrected 4-lane v9.1 is pending.

## V10/V11 Final Software Checks

Why v10 was needed:

- User reported corrected 4-lane v9.1 still showed gray.
- Re-reading the author's files found that two different reference sets had been mixed:
  - ATK/ALIENTEK reference: `dts参考.txt` and `ATK rk3506 timing.txt`
    - `C1 0F 0C`
    - `3A 77`
    - hfp/hsync/hbp `8/2/43`
    - vfp/vsync/vbp `4/10/42`
    - 2-lane DSI reference
  - Taishan RK3566 reference:
    - `C1 12 0C`
    - `3A 70`
    - hfp/hsync/hbp `7/2/11`
    - vfp/vsync/vbp `22/2/12`
- V9/V9.1 had used the Taishan values. V10 returned to the ATK/ALIENTEK values that match the user's pasted DTS/reference.

V10 result:

- Local source: `artifacts\kernel\panel-sitronix-st7701-v10-d300-atk-dtsref-2lane.c`
- Local module: `artifacts\kernel\panel-sitronix-st7701-v10-d300-atk-dtsref-2lane.ko`
- SHA256: `3d6b85f27b681d7cf1c5285d3cc779b1de28b86db59f0b96bf016789982d98be`
- Board backup before v10: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v9.1-correct-4lane-before-v10-atk-dtsref-2lane-20260702.bak`
- Runtime:
  - `prepare: lanes=2 format=0 mode_flags=0x803`
  - `fb0` exists as `480x854`, `16bpp`
  - `PB1 reset` is `out hi`
  - `PD8 panel-dsi-backlight` is `out hi`
  - `PZ1` is unclaimed/free
  - `FB_BARS_WRITTEN 480 854 1024 16`

V11 BIST diagnostic:

- Local source: `artifacts\kernel\panel-sitronix-st7701-v11-d300-atk-dtsref-2lane-bist.c`
- Local module: `artifacts\kernel\panel-sitronix-st7701-v11-d300-atk-dtsref-2lane-bist.ko`
- SHA256: `28fd3a08d306755943fdb11c4f5a97ecfbe45aaead14b41e107033452bb070ea`
- Board backup before v11: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v10-atk-dtsref-2lane-before-v11-bist-20260702.bak`
- Difference from v10:
  - Adds only the vendor-commented ST7701S BIST sequence:
    - `FF 77 01 00 00 12`
    - `D1 81`
    - `D2 07`
- Runtime:
  - `prepare: lanes=2 format=0 mode_flags=0x803`
  - `fb0` exists as `480x854`, `16bpp`
  - `PB1 reset` is `out hi`
  - `PD8 panel-dsi-backlight` is `out hi`
  - `PZ1` is unclaimed/free

Interpretation:

- If v11 BIST shows an internal test pattern, DSI command decode is working and the remaining issue is likely DSI video stream mode, clocking, lane speed, or timing.
- If v11 BIST remains gray, continued software attempts on porch/pixel format are unlikely to be productive. The next required evidence is electrical/hardware: LCD 5 V, reset waveform at the connector, backlight voltage, FPC orientation, and DSI lane activity/order.

V11 visual result:

- User reported v11 still shows only backlight with no display.
- Active module was restored from v11 BIST back to normal v10 ATK/ALIENTEK DTS-reference module.
- Current active module SHA256: `3d6b85f27b681d7cf1c5285d3cc779b1de28b86db59f0b96bf016789982d98be`
- BIST module backup: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v11-bist-before-restore-v10-20260702.bak`
- BIST backup SHA256: `28fd3a08d306755943fdb11c4f5a97ecfbe45aaead14b41e107033452bb070ea`

Current conclusion:

- The software stack can create a DRM/fb0 output and drive the configured reset/backlight GPIOs.
- The ST7701S BIST command not producing a visible pattern means the panel is probably not decoding commands, not powered/reset correctly at the actual connector, or the physical DSI lane wiring/orientation is wrong.
- Do not continue random DTB/panel-timing attempts until the following are measured at the LCD connector:
  - LCD 5 V supply.
  - DSI reset/PB1 voltage and boot-time pulse.
  - Backlight/PD8 voltage.
  - FPC orientation and connector pin numbering.
  - DSI clock and D0/D1 activity, with polarity/order checked. D0 is especially important because DSI command mode should use lane 0.
