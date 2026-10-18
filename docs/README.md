# 文档索引

本目录汇集项目的架构、协议、项目材料与设计原型。所有文档以 **2026-07 的最终实现** 为准；历史设计稿只作追溯。

## 架构与图表

- [`architecture/edgefocus-complete-architecture.md`](architecture/edgefocus-complete-architecture.md) — 系统架构母文档（产品、会话、AI、异构硬件、交互、演示与部署）
- [`architecture/A-core-diagrams.md`](architecture/A-core-diagrams.md) — 核心图（系统闭环、异构分工、AI 融合、安全岛）
- [`architecture/B-engineering-diagrams.md`](architecture/B-engineering-diagrams.md) — 工程图（会话、云台、M33、供电、故障矩阵）
- [`architecture/C-complete-diagram-atlas.md`](architecture/C-complete-diagram-atlas.md) — 完整图谱
- [`architecture/poster/`](architecture/poster/) — 海报源文件、渲染图与打印 PDF
- [`architecture/source_pack/`](architecture/source_pack/) — 学术风格图源（PNG/SVG）

## 跨端协议

- [`protocols/README-当前文档入口.md`](protocols/README-当前文档入口.md) — 协议文档阅读顺序
- [`protocols/EdgeFocus-最终API契约-App主机.md`](protocols/EdgeFocus-最终API契约-App主机.md) — App ↔ 主机 GATT / JSON 契约
- [`protocols/EdgeFocus-最终API契约-主机手环.md`](protocols/EdgeFocus-最终API契约-主机手环.md) — 主机 ↔ 手环帧格式
- [`protocols/P0-协议冻结清单.md`](protocols/P0-协议冻结清单.md) — 冻结字段与服务/特征 UUID
- [`protocols/EdgeFocus-最终最终版主链路文档.md`](protocols/EdgeFocus-最终最终版主链路文档.md) — 产品与工程主口径
- [`protocols/EdgeFocus-最终工作流程与交互时机.md`](protocols/EdgeFocus-最终工作流程与交互时机.md) — 主链路时序
- [`protocols/P0-接口变更流程.md`](protocols/P0-接口变更流程.md) — 协议变更流程

## 项目材料

- [`project/EdgeFocus_项目总介绍_嵌入式大赛版.md`](project/EdgeFocus_项目总介绍_嵌入式大赛版.md)
- [`project/EdgeFocus_技术亮点与创新优势.md`](project/EdgeFocus_技术亮点与创新优势.md)
- [`project/EdgeFocus_系统架构与实现说明.md`](project/EdgeFocus_系统架构与实现说明.md)
- [`project/EdgeFocus_答辩展示提纲.md`](project/EdgeFocus_答辩展示提纲.md)
- [`project/EdgeFocus_报告书目录与撰写建议.md`](project/EdgeFocus_报告书目录与撰写建议.md)
- [`project/EdgeFocus_报告书章节素材库.md`](project/EdgeFocus_报告书章节素材库.md)

## 设计原型

- [`design/ui-prototypes/`](design/ui-prototypes/) — 早期 HTML 交互原型与设计说明
- [`design/compose-prototype/`](design/compose-prototype/) — Compose 界面原型源码

## 图片

- [`images/`](images/) — App 截图与设备照片

## 模块内文档

各模块还有更贴近代码的文档：

| 模块 | 入口 |
| --- | --- |
| Android | [`../android-app/docs/architecture/edgefocus-app-architecture.md`](../android-app/docs/architecture/edgefocus-app-architecture.md) |
| 自研主机 | [`../host/self-developed/docs/architecture/current-architecture.md`](../host/self-developed/docs/architecture/current-architecture.md) |
| 正点原子主机 | [`../host/alientek/docs/项目介绍和代码结构说明_代码接手.md`](../host/alientek/docs/项目介绍和代码结构说明_代码接手.md) |
| 云台固件 | [`../firmware/gimbal-g431/docs/uart_command_table.md`](../firmware/gimbal-g431/docs/uart_command_table.md) |
| 手环固件 | [`../firmware/ble-band/stm32/doc/BLE_Signal_Protocol.md`](../firmware/ble-band/stm32/doc/BLE_Signal_Protocol.md) |
| M33 风扇 | [`../firmware/m33-fan-controller/docs/stm32mp257-m33-fan-inventory.md`](../firmware/m33-fan-controller/docs/stm32mp257-m33-fan-inventory.md) |
| Linux 适配 | [`../linux/bsp/CODE_AGENT_ENTRY.md`](../linux/bsp/CODE_AGENT_ENTRY.md) |
| 硬件设计 | [`../hardware/README.md`](../hardware/README.md) |
