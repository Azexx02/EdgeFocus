# 自研底板 Linux 适配资料

本目录是自研 STM32MP257 载板的 Linux 侧 bring-up 参考包：设备树、内核模块片段、启动备份、板端脚本与实测记录。它**不是完整系统镜像**，也不替代 OpenSTLinux 发行包。

## 板级事实

| 项目 | 实测状态 |
| --- | --- |
| 主控 | STM32MP257D（双 Cortex-A35 + M33），2 GB DDR |
| 系统 | OpenSTLinux（厂商 5.0.3 snapshot BSP 派生） |
| 网络 | RTL8723DU Wi-Fi/BT，经 CH334R USB Hub；无有线以太网 |
| NPU | Vivante/Galcore，`/dev/galcore` 存在时可跑真实推理 |
| 风扇 | PB10 / TIM17_CH1N 25 kHz PWM（外部驱动反相），PB11 tach；v17 DTB 验证 |
| 显示 | MIPI DSI / SPI 屏 bring-up 未完成，不作为核心功能依赖 |
| 音频 | MAX98357A + ICS-43434，部分验证，无稳定可听结论 |
| 相机 | USB 摄像头，按 `/dev/video*` 枚举 |

## 设备树

`artifacts/dtb/` 保留了从厂商原始 DTB 到自研板 v17 的完整演进链（`.dts` + `.dtb`）：

```text
stm32mp257d-atk-ddr-2GB-selfcarrier-safe-v9-gpu-okay
  -> v10 ~ v15 音频链路（MAX98357A / I2S / 时钟）
  -> v16b 显示与 UART7/PD3 调整
  -> v17-fan-pb10-pwm-pb11-tach   # 当前基线
```

- v17 关键取舍：禁用 `can@402d0000`（其 AF7 占用 PB11，会阻断 tach），因此 CAN 接口由 3 个变为 2 个。
- `clean_dtb_backup_20260630_214811/` 保存原始/干净 DTB 与启动配置，便于回滚。
- `artifacts/dtb/chatgpt_device_tree_original_vs_selfcarrier.diff` 给出厂商原始树与自研树的差异。

## 风扇服务（Linux fallback）

`scripts/fan_temp_control.py` + `scripts/fan-temp-control.service` 是 M33 接管前的 Linux 温控实现。M33 固件上线后，Linux 的 PWM/timer 必须解绑，二者互斥；所有权切换与健康检查见 [`../../firmware/m33-fan-controller/`](../../firmware/m33-fan-controller/)。

## 目录

| 路径 | 内容 |
| --- | --- |
| `CODE_AGENT_ENTRY.md` | 给代码开发者的阅读顺序与边界 |
| `MATERIAL_AUDIT.md` | 资料取舍说明 |
| `status/` | 当前状态、进度、屏幕/音频结论、硬件说明 |
| `docs/` | 板级笔记、安装、CH334R/RTL8723DU 审计、网表与 DT 说明 |
| `hardware/` | 网表与硬件手册节选 |
| `artifacts/dtb/` | DTB/DTS 变体与差异 |
| `artifacts/kernel/` | 屏幕/音频相关内核模块源码与 `.ko` 片段 |
| `artifacts/selected_logs_and_sources/` | 关键 dmesg、pinmux、时钟与驱动片段 |
| `clean_dtb_backup_20260630_214811/` | 干净 DTB 与启动配置备份 |
| `scripts/` | 备份、Wi-Fi 驱动编译、内核符号、fb 测试、磁盘镜像流式备份 |
| `logs/` | 原始启动与诊断日志 |
| `tools/` | 本地显示/帧缓冲测试工具 |

## 建议阅读顺序

1. [`CODE_AGENT_ENTRY.md`](CODE_AGENT_ENTRY.md)
2. [`status/progress.md`](status/progress.md)
3. [`docs/board_netlist_device_tree_notes.md`](docs/board_netlist_device_tree_notes.md)
4. 需要底层排查时再进入 `artifacts/`、`logs/`、`hardware/`

更完整的资料说明见 [`docs/hardware_pack_overview.md`](docs/hardware_pack_overview.md)。

## 使用边界

- 不要直接把某个 `.dtb` 覆盖到板端而不保留原文件；先备份 `/boot` 并确认启动配置。
- 屏幕与音频仍是实验状态，不能作为产品硬依赖。
- 板端 IP 不固定；部署前先确认当前地址。
