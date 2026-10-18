# 第三方组件与资料说明

本项目在开发与联调过程中使用了以下第三方组件、SDK 与厂商资料。其版权归各自作者或公司所有，本仓库仅保留为复现与说明所必需的部分。

## 芯片与板级支持

- **STMicroelectronics**：STM32MP257 / STM32G431 / STM32G030 HAL、CMSIS、OpenAMP、STM32MP2 SDK、STAI_MPU。参考 ST 官方许可。
- **正点原子（ALIENTEK）**：ATK-DLMP257B 核心板与开发板资料、CubeMP2 示例工程（`OpenAMP_TTY_echo` 等）、硬件手册。厂商 SDK 与手册未随本仓库分发。
- **立创 EDA Pro**：硬件工程文件格式与编辑器。

## 软件库

- **OpenCV**：相机采集与图像预处理（可选依赖）。
- **SQLite**：会话、事件与报告持久化。
- **BlueZ**：Linux BLE GATT Server / Client。
- **OpenAMP / RPMsg**：A35 ↔ M33 通信。
- **ESP-IDF / NimBLE**：ESP32 旧硬件手环固件。
- **AndroidX / Jetpack Compose / Hilt / Room / Kotlin**：Android 客户端。
- **Ultralytics YOLO11n-Pose**：姿态模型基础网络，量化后以 `.nb` 形式部署到 NPU。

## 模型与数据

`host/*/models/` 中的 `.nb` 为本项目导出的板端推理模型；`.onnx` 与训练数据不随仓库分发。

## 字体与图片

海报与图表使用系统字体或开源字体渲染；设备照片、PCB 图与 App 截图由作者拍摄或生成。

## 许可

- 本项目原创代码、文档与硬件设计采用 **Apache License 2.0**，见 `LICENSE`。
- `models/**/*.nb` 模型权重由 Ultralytics YOLO11n-Pose 导出，按 **AGPL-3.0** 分发，见 `MODEL_LICENSE.md` 与 `LICENSES/AGPL-3.0.txt`。
- 第三方组件遵循其原始许可证，清单见 `LICENSES/README.md`。
