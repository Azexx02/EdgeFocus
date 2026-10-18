# MAX98357A Playback DTB Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable the self-developed carrier board's MAX98357A speaker path as a playback-only ALSA card.

**Architecture:** Base the change on the currently working GPU/NPU DTB source, add a minimal MAX98357A codec node, a simple-audio-card node, and enable I2S1 with PH10/PH3/PH7 AF2 pinmux. Keep ES8388, SPI1, the existing disabled audio-graph-card, ICS-43434 mic, and `focus_app` sound settings untouched.

**Tech Stack:** STM32MP257 Linux 6.6.48 device tree, `dtc`, ALSA `simple-audio-card`, `snd-soc-max98357a`, `snd-soc-stm32-i2s`.

---

### Task 1: Create the DTB variant

**Files:**
- Read: `D:/Aze/Documents/developMP257/artifacts/dtb/stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v9-gpu-okay.dts`
- Create: `D:/Aze/Documents/developMP257/artifacts/dtb/stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v10-max98357a-playback.dts`
- Create: `D:/Aze/Documents/developMP257/artifacts/dtb/stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v10-max98357a-playback.dtb`

- [x] Copy the GPU/NPU-fixed DTS to the v10 MAX98357A DTS.
- [x] Add `audio-codec-max98357a` with `sdmode-gpios = <0x2f 0x03 0x00>`.
- [x] Add `sound-max98357a` as a `simple-audio-card` using I2S1 phandle `<0xdf>`.
- [x] Add I2S1 pinctrl nodes under `pinctrl@44240000`.
- [x] Enable `audio-controller@40230000` and assign the new pinctrl nodes.
- [x] Compile the new DTB with `dtc`.

### Task 2: Deploy and verify

**Board:**
- Host: `192.168.10.171`
- Boot DTB: `/boot/stm32mp257d-atk-ddr-2GB.dtb`

- [x] Copy the new DTB to `/tmp` on the board.
- [x] Back up the current board DTB before replacing it.
- [x] Replace `/boot/stm32mp257d-atk-ddr-2GB.dtb`, run `sync`, and reboot.
- [x] Verify `/proc/asound/cards`, `aplay -l`, and audio-related `dmesg`.
- [x] If the card appears, run a short `speaker-test`.
- [x] Record the outcome in `progress.md`.

Result: v15 is currently installed. v10 failed on PD3 `sdmode-gpios`; v12/v13 failed on I2S clock/MCLK rate; v14 broke I2S probe and was rolled back. v15 keeps PD3 and MCLK out of the card, assigns `CK_KER_SPI1` to 49.152 MHz, and adds 2x32 TDM slots. It registers the card and accepts `48000/S16_LE` playback, but rejects `S32_LE`, `S24_LE`, and `44100/S16_LE`. `clk_summary` still shows `ck_flexgen_16 -> ck_ker_spi1` around 9.836 MHz rather than 49.152 MHz, so the remaining root problem is clock-tree/control plus the unresolved AMP_EN/PD3 hardware enable question.
