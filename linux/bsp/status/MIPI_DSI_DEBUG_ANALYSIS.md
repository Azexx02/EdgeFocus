# MIPI DSI Debug Analysis

Date: 2026-07-02

## Current Request

Pause further parameter experiments and整理 the known facts, tried changes, open hypotheses, and next debugging plan before continuing. Avoid losing important details after many iterations.

## Board Access And Current Caution

- Board SSH target used during this phase: `root@192.168.10.173`.
- Serial console: `COM16`, 115200.
- Last action before this整理: v8.6 diagnostic panel module was installed and the board was rebooted.
- After that reboot, the board entered a real watchdog reset loop. Serial showed normal Linux boot reaching `panel_sitronix_st7701` load and DRM init, then `IWDG1 system reset` before userspace completed.
- Recovery was done from U-Boot over COM16 by booting once with `modprobe.blacklist=panel_sitronix_st7701`, restoring the v8.5 vendor-only module backup, running `depmod -a`, syncing, and rebooting.
- Current clean baseline after recovery:
  - SSH works at `root@192.168.10.173`.
  - `systemctl --failed` reports 0 failed units.
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko` SHA256 matches `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.5-vendor-only.bak`: `275824a34be068c00b21e7c33cc6f0585739a0bc73c5d5ef81780009950712a5`.
- After inspecting STM32 lane-rate derivation, v8.7 was installed as the current active experiment:
  - Source/artifact: `artifacts/kernel/panel-sitronix-st7701-v8.7-d300-vendor-burst.c/.ko`.
  - Board backup before install: `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.5-before-v8.7-burst.bak`.
  - SHA256: `5359f7405a20e4dd434ded487220a33b95f2040fb0743a2c5df22ea9b0aeccf6`.
  - Runtime log: `prepare: lanes=2 format=0 mode_flags=0x803`.
  - `fb0=480x854`, `stride=1024`, `16bpp`; system has 0 failed units.
  - RGB565 color bars were written directly to `/dev/fb0`; visual result is pending.

## Panel And Wiring Facts

- Panel product/model from user/reference: `D300T9307V1`.
- Size/resolution: 3.0 inch, `480x854`.
- Driver IC: `ST7701S`.
- Display interface described by reference: MIPI video mode, also notes `RGB + 3line SPI` capability.
- Touch interface: I2C, FT or CST series.
- Self-carrier MIPI connector routes:
  - DSI clock pair.
  - DSI data lane 0.
  - DSI data lane 1.
  - DSI lanes 2 and 3 are not routed.
- Therefore the correct display lane count is 2 lanes.
- User-provided GPIO mapping for MIPI:
  - `DSI_RESET -> PB1`
  - `DSI_BL -> PD8`
  - `LCD_ID -> PZ1`
- Touch later:
  - `CTP_SCL -> PZ4 / I2C8_SCL`
  - `CTP_SDA -> PZ9 / I2C8_SDA`
  - `CTP_RST -> PB4`
  - `CTP_INT -> PB5`

## Vendor Reference Data

The user-provided DTS snippet for the 3.0 inch screen says:

- `myflag = "3p0_480p_by_nanshou"`
- `rockchip,lane-rate = <500>`
- Init sequence is encoded as packet type, delay, payload length, payload bytes.
- Timing:
  - `clock-frequency = <27000000>`
  - `hactive = <480>`
  - `vactive = <854>`
  - `hfront-porch = <8>`
  - `hsync-len = <2>`
  - `hback-porch = <43>`
  - `vfront-porch = <4>`
  - `vsync-len = <10>`
  - `vback-porch = <42>`
  - `hsync-active = <0>`
  - `vsync-active = <0>`
  - `de-active = <0>`
  - `pixelclk-active = <1>`
- Init sequence ends with:
  - `36 00`
  - `3A 77`
  - `11` with `0x78` delay, 120 ms.
  - `29` with `0x14` delay, 20 ms.
- Important detail newly noticed:
  - `15 64 02 D0 88` means command `D0 88` has a 100 ms post-delay in the reference table.
  - Earlier driver conversion sent `D0 88` but did not preserve this per-command delay.
- Another important detail:
  - Reference lane rate is 500 Mbps on Rockchip. Current STM32 path only set mode timing/format/lanes through DRM; no explicit STM32 DSI lane-rate equivalent has been confirmed yet.

## Additional Vendor Package: D300T9307V1更新时间2024-11-14

Path:

- `D:\Aze\Documents\developMP257\D300T9307V1更新时间2024-11-14`

Relevant contents found:

- `D300T9307V1 SPEC\D300T9307V1 SPEC.pdf`
- `D300T9307V1 SPEC\ST7701S_SPEC_Preliminary V0.2.pdf`
- `D300T9307V1 SPEC\D300N9307V0.txt`
- `480854触摸屏实验新版\HARDWARE\LCD\lcd.c`
- `480854触摸屏实验新版\HARDWARE\LCD\lcd.h`
- `480854触摸屏实验新版\HARDWARE\LCD\ltdc.c`
- `触摸TOUCH\FT5316...`

PDF/text facts, low priority for display bring-up:

- `D300T9307V1 SPEC.pdf` confirms:
  - `480*RGB*854`
  - interface: `MIPI Video Mode&(RGB+3line SPI)`
  - driver: `ST7701S`
  - backlight: white
  - MIPI pin table includes D0, D1, clock, reset, LEDA/LEDK.
- `ST7701S_SPEC_Preliminary V0.2.pdf` confirms:
  - ST7701S supports `480*RGB*854`.
  - ST7701S supports MIPI/RGB/SPI interfaces.
  - MIPI DSI supports 1 clock lane plus 1 or 2 data lanes.
  - Max speed: 1 lane 800 Mbps, 2 lanes 550 Mbps.

Important source-code facts:

- `D300N9307V0.txt` is a direct register-init example using 3-line SPI style writes:
  - Command write sends a leading `0` bit, then 8 command bits.
  - Data write sends a leading `1` bit, then 8 data bits.
  - This is not ordinary 8-bit SPI; it is 9-bit / 3-wire command-data encoded SPI.
- `480854触摸屏实验新版\HARDWARE\LCD\ltdc.c` also initializes the panel through `RGB_SPI_Init()` and the helper functions:
  - `RGB_IO_SPI_writecom()`: leading `0` bit + 8 command bits.
  - `RGB_IO_SPI_writedat()`: leading `1` bit + 8 data bits.
- This confirms the panel IC can be initialized through `RGB + 3line SPI`, but it should not override the author's explicit Linux/Rockchip MIPI DTS snippet.
- The author specifically says the display portion is brought up by modifying the ALIENTEK DTS `dsi` node and provides `panel-init-sequence` as MIPI DSI packets. Therefore, for the current MIPI connector, DSI packet initialization remains the primary path.
- The资料包 is more useful for TP/touch later and as a cross-check of register values, not as the main display initialization method.

Differences between currently used init table and package examples:

- `D300N9307V0.txt` and `ltdc.c` use:
  - `C1 12 0C`
  - final `3A 55`, comment says `0x77 //888  0x55 //565  0x66 //666`
- The earlier OSHWHub DTS snippet provided by the user used:
  - `C1 0F 0C`
  - final `3A 77`
- The STM32F429 `ltdc.c` timing for LCD ID `0x5048` uses:
  - width `480`
  - height `854`
  - hsync `10`
  - vsync `10`
  - hbp `11`
  - vbp `13`
  - hfp `7`
  - vfp `22`
  - pixel clock comment `27M`
- The DTS snippet / `D300N9307V0.txt` use:
  - hsync `2`
  - vsync `10`
  - hbp `43`
  - vbp `42`
  - hfp `8`
  - vfp `4`
  - pixel clock `27M`
- Both timing sets total to a similar pixel clock range but are not identical. Timing is now an open variable, but command-interface mismatch is higher priority.

## Software State Known Good Outside Display

- Wi-Fi and SSH had been stable before the latest v8.6 reboot.
- Bluetooth is working with Radxa RTL8723DU firmware/config.
- Systemd boot cleanup is intentionally applied for self-carrier hardware.
- Audio is paused and unresolved.

## Tried Display Configurations

### Official ALIENTEK MIPI Driver Path, v7.x

- Used `compatible = "alientek,mipi-lcd"` and official `dsi_select_id` values.
- Tried 4 lane then corrected to 2 lane after schematic showed only lanes 0 and 1.
- Tried `dsi_select_id` values 0, 1, 2, 3.
- Results:
  - DSI connected.
  - Framebuffer existed.
  - Modes were official-panel modes like `720x1280` or `1080x1920`, not this screen's `480x854`.
- Conclusion:
  - Official ALIENTEK selector path does not provide the needed `480x854` D300T9307V1 mode.

### ST7701S Driver Path, v8.x

Created a new `nanshou,d300t9307v1` entry in `panel-sitronix-st7701.c`.

Common runtime facts after v8:

- `panel_sitronix_st7701` loads.
- DRM reports `DSI-1 connected`.
- `fb0` becomes `480x854`, 16 bpp, stride `1024`.
- `modetest` reports preferred mode `480x854`.
- Framebuffer color-bar writes complete.
- User-visible result remains gray.

Version notes:

- v8:
  - First D300T9307V1 ST7701S module and DTB.
  - Reset was configured active-low.
  - GPIO debug later showed PB1 physically low, likely holding panel in reset.
- v8.1:
  - Changed reset GPIO to active-high for this Linux ST7701 driver.
  - PB1 became physically high.
  - Still gray.
- v8.2:
  - Changed DSI flags from video burst + LPM + non-continuous clock to plain video + LPM.
  - Still gray.
- v8.3:
  - Added DSI write failure logging.
  - Logs show `prepare`, `enable display on`, no `DSI write failed`.
  - Still gray.
- v8.4:
  - Tested RGB565: DCS `3A=0x55` and DSI host `MIPI_DSI_FMT_RGB565`.
  - Corrected build logs `format=3`.
  - Framebuffer remained `480x854/16bpp/stride=1024`.
  - Still gray.
- v8.5:
  - Reverted to vendor reference pixel format `3A=0x77` / RGB888.
  - Added `vendor_sequence_only` so D300T9307V1 skips the generic `st7701_init_sequence()` and only runs the vendor table.
  - Logs showed `prepare: using vendor sequence only`.
  - Important correction after source review: v8.5 already preserves the author's `D0 88` 100 ms delay and the final `11` 120 ms / `29` 20 ms delays.
  - Still gray.
- v8.6:
  - Diagnostic version based on v8.5.
  - Adds DCS readback of `0x0A/0x0B/0x0C/0x0D/0x04` after display-on.
  - Installed and rebooted, then triggered a watchdog reset loop shortly after `panel_sitronix_st7701`/DRM init.
  - No useful readback result was collected. Treat DCS readback on this STM32/ST7701 path as unsafe unless the watchdog is disabled or a timeout-safe read path is implemented.
- v8.7:
  - Single-variable test based on v8.5.
  - Changed only `dsi->mode_flags` from `0x801` to `0x803`, adding `MIPI_DSI_MODE_VIDEO_BURST`.
  - DTB, timing, pixel format, lane count, and init sequence were unchanged.
  - Board boots stably; visual result pending.

## Key Observations And Meaning

- Linux-side DRM being connected is not proof the physical panel is decoding data. It only proves the host pipeline bound.
- No DSI write error is not proof the panel accepted commands; DSI host may accept TX without physical panel response.
- Gray screen means backlight/panel power is probably present at some level, but not necessarily that the LCD controller is initialized or receiving valid video.
- The strongest unresolved software questions are:
  - Are DSI commands actually reaching/being decoded by the panel?
  - Is STM32 DSI lane speed compatible with a reference requiring 500 Mbps lane rate?
  - Did missing per-command delays, especially after `D0 88`, break ST7701S power/init sequencing?
  - Does this module require 3-line SPI for initialization despite the reference table being representable as MIPI DSI packets?
- The strongest unresolved hardware questions are:
  - Is `PB1` definitely the physical `MIPI_RESET` line at the LCD connector?
  - Is `PD8` definitely the physical `LCD_BL` line?
  - Are panel 5 V and ground correct at the connector?
  - Is FPC orientation and pin numbering correct?
  - Are DSI lane polarity/order and lane 0/1 mapping correct?

## Hypotheses Ranked

1. STM32 DSI lane speed is too low or not matching the panel.
   - Evidence for: the author's DTS explicitly sets `rockchip,lane-rate = <500>`.
   - ST7701S spec says 2-lane MIPI can support up to 550 Mbps, so 500 Mbps is plausible and intentional.
   - STM32 finding: no DT `lane-rate` property was found. MP25 PHY141 computes lane rate from mode pixel clock, bits-per-pixel, lane count, and `MIPI_DSI_MODE_VIDEO_BURST`.
   - Current v8.5 inferred lane rate is about `27MHz * 24 / 2 = 324Mbps`.
   - v8.7 burst mode should raise the inferred data rate by about 20%, but still not to 500 Mbps.
2. The DSI packet sequence is close but not exact enough.
   - Evidence for: the author's display instructions explicitly use `panel-init-sequence` under `&dsi`, not the MCU 3-line SPI path.
   - Evidence against: v8.5 already preserves the visible DTS packet order and major delays including `D0 88`, `11`, and `29`.
   - Remaining possible mismatch: packet type selection or host-side low-power/high-speed command mode may still differ from Rockchip's generic `panel-init-sequence` implementation.
3. DSI command path is not actually talking to the panel, despite host-side success.
   - Evidence for: all framebuffer and timing attempts succeed in Linux but panel remains gray.
   - Next evidence: v8.6 DCS readback.
4. Reset/backlight physical mapping is still wrong.
   - Evidence for: historical netlist mismatch concern, and gray screen can be misleading.
   - Next action: electrical measurement or GPIO toggle at LCD connector pins.
5. Pixel format mismatch.
   - Evidence against: RGB888 and RGB565 both tested with no visible change.
   - Lower priority now, and the author's DSI snippet explicitly uses `3A 77`.
6. Panel requires 3-line SPI initialization instead of DSI packet initialization.
   - Evidence for: MCU examples use 3-line SPI; spec phrase includes `RGB+3line SPI`.
   - Evidence against: the author's Linux display instructions explicitly use DSI `panel-init-sequence`; current connector documentation does not show separate display 3-line SPI pins.
   - Low priority unless DSI readback and exact DTS sequence both fail and hardware confirms 3-line SPI pins are routed.

## Next Debug Plan

Do not make multiple display changes at once.

1. Keep the board on the clean v8.5 baseline while investigating.
   - Current active test is v8.7; rollback to v8.5 with:
     `cp -f /lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v8.5-before-v8.7-burst.bak /lib/modules/6.6.48/extra/panel-sitronix-st7701.ko && depmod -a && sync && reboot`
   - Do not reinstall v8.6 readback unless there is a watchdog-safe plan.
2. If v8.7 remains gray, choose the next lane-rate test deliberately:
   - Option A: alter panel mode pixel clock to target about 500 Mbps, accepting changed refresh/blanking as a test.
   - Option B: patch/rebuild the STM32 DSI host path to force lane_mbps independently, closer to Rockchip `lane-rate`, but this requires replacing the kernel Image or built-in driver path and has higher boot risk.
3. If lane-rate tests still leave gray screen:
   - Stop software guessing.
   - Measure LCD connector:
     - pin 1 5 V.
     - reset pin during boot.
     - backlight pin during boot.
     - DSI clock/data activity if equipment is available.
   - Confirm FPC orientation and pin numbering.
4. If continuing after measurement:
   - Compare and choose one timing set per test:
     - DTS/spec timing: hsync/hbp/hfp `2/43/8`, vsync/vbp/vfp `10/42/4`.
     - STM32F429 demo timing for ID `0x5048`: hsync/hbp/hfp `10/11/7`, vsync/vbp/vfp `10/13/22`.
   - Prefer DTS/spec timing first because the author's display bring-up instruction specifically uses it.

## Files To Check Before Continuing

- `D:\Aze\Documents\developMP257\SCREEN_BRINGUP_STATUS.md`
- `D:\Aze\Documents\developMP257\progress.md`
- `D:\Aze\Documents\developMP257\artifacts\kernel\panel-sitronix-st7701-v8.5-d300-vendor-only.c`
- `D:\Aze\Documents\developMP257\artifacts\kernel\panel-sitronix-st7701-v8.6-d300-readback.c`
- `D:\Aze\Documents\developMP257\clean_dtb_backup_20260630_214811\stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v8.1-st7701-d300t9307v1-reset-active-high.dts`
