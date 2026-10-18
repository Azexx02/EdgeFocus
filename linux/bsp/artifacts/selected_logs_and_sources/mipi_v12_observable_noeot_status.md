# MIPI DSI v12 状态记录

时间：2026-07-06

## 当前软件状态

- 当前系统 DTB 仍是 `stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v16b-disable-uart7-pd3-sdmode.dtb`，没有继续改 DTB。
- 已安装新的 panel 模块：
  - 本地源码：`artifacts/kernel/panel-sitronix-st7701-v12-d300-observable-noeot.c`
  - 本地模块：`artifacts/kernel/panel-sitronix-st7701-v12-d300-observable-noeot.ko`
  - 板端模块：`/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko`
  - SHA256：`ec3189ee92d2057e01d56d87641ff42cb5ceea740bb671daa5221549f4b498dd`
- 板端旧 v10 模块已备份：
  - `/lib/modules/6.6.48/extra/panel-sitronix-st7701.ko.v10-before-v12-observable-noeot.bak`

## v12 相对 v10 的变化

- 固定在 2-lane DSI、RGB888、27 MHz 参考 timing。
- `mode_flags` 从 `0x803` 改为 `0xa03`：
  - `VIDEO`
  - `VIDEO_BURST`
  - `NO_EOT_PACKET`
  - `LPM`
- 没有做 DCS readback，避免之前 readback 触发 watchdog reset loop。
- 增加 probe/prepare/reset/enable 日志。
- 每条 DSI DCS write 都打印返回值。
- 若任意 DSI write 返回负数，prepare 会返回错误，不再假装初始化成功。

## 板端验证结果

重启后系统能正常进入 Linux。关键日志保存在：

- `artifacts/dmesg_mipi_v12_observable_noeot.log`

日志显示：

- `probe: v12 observable no-eot lanes=2 format=0 mode_flags=0xa03`
- `prepare: lanes=2 format=0 mode_flags=0xa03`
- reset 流程执行到：
  - `drive inactive before power`
  - `regulators enabled`
  - `release panel reset`
- 作者参考 DSI 初始化序列全部通过 `mipi_dsi_dcs_write_buffer()` 发出。
- 所有 DSI write 都返回正数，没有负数错误。
- `prepare complete: DSI init sequence sent`
- `enable display on`
- `/dev/fb0` 存在，`fb0 virtual_size = 480,854`，`bits_per_pixel = 16`

## 当前测试动作

- 已向 `/dev/fb0` 写入 RGB565 色条。
- 已打开 STM32 DSI VPG：
  - `/sys/kernel/debug/48000000.dsi/vpg = 1`
  - `/sys/kernel/debug/48000000.dsi/vpg_horizontal = 1`
- 背光 sysfs 状态：
  - `/sys/class/backlight/panel-dsi-backlight/brightness = 1`

## 当前判断

这次 v12 证明软件侧 DRM/LTDC/DSI graph、panel probe、prepare、DSI command 发送路径都没有报告错误。若 VPG 打开后仍然黑屏，优先怀疑方向应转到板级实测：

- DSI 接口 1/2 脚 5V 是否真实到屏。
- DSI 接口 7 脚 `LCD_BL/DSI_BL` 是否真实为高，且背光 LEDA/LEDK 有电压/电流。
- DSI 接口 25 脚 `MIPI_RESET/DSI_RESET` 是否有正确 reset 脉冲，而不是只看最终电平。
- DSI CLK 和 D0 lane 是否有活动，尤其是 P/N 是否接反或线序不一致。
- 屏幕模块 IM strap 是否确实是 MIPI 模式。

如果屏幕仍无任何显示，下一步不建议继续随机改 timing 或 lane-rate；先用示波器/逻辑分析仪在屏幕连接器侧确认供电、背光、reset pulse、DSI clock/lane0 活动。
