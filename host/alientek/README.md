# 主机软件 · 正点原子 ATK-DLMP257B 版

运行在正点原子 ATK-DLMP257B（STM32MP257D，2 GB DDR）上的 EdgeFocus 主机软件，是**正点原子硬件路线的最终版本**。

## 组成

```text
focus_app/
├── main.cpp                 # 进程入口：AppContext + 线程装配
├── app/                     # 上下文、日志、状态存储
├── camera/                  # 摄像头采集与共享帧
├── vision/                  # NPU 姿态推理、预处理、VisionResult
├── behavior/                # 行为状态机、手机事实、事件检测
├── policy/                  # 提醒等级、冷却与干预命令
├── device/                  # 声音、屏幕、手环 BLE
├── network/                 # App BLE GATT（FF00/FF01/FF02）、HTTP Demo
├── storage/                 # SQLite 会话、事件、报告
├── ui/                      # SPI LCD 与触摸
├── config/                  # 模型注册表、运行配置
├── models/                  # edgefocus_pose_v1（.nb + 配置）
├── tests/                   # CTest 模块测试
├── scripts/                 # 板端辅助脚本
└── CMakeLists.txt
```

## 主流程

```text
Camera / App signals -> behavior -> policy -> device reminders
                     -> storage/report -> App history/feedback
```

关键事实：

- 相机、NPU、BLE、SQLite 都由 `AppContext` 持有；会话状态由主机决定。
- App 通过 FF00/FF01/FF02 与服务通信；手环使用 0x180C/0x2A56。
- 语音提示已替换为生成提示音；屏幕与声音属于可选提醒渠道。
- `models/edgefocus_pose_v1/` 是运行时模型包，移动目录需同步 `config/model_registry.json`。

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

本地 Windows 构建可能需要 Visual Studio / nmake 环境；板端交叉编译使用 STM32MP2 SDK，具体见 `deploy/` 脚本与下方文档。

## 板端部署

`deploy/` 的部署与诊断脚本使用以下环境变量：

| 环境变量 | 用途 |
| --- | --- |
| `EDGEFOCUS_BOARD_IP` | 板端 IP |
| `EDGEFOCUS_BOARD_USER` | SSH 用户（默认 root） |
| `EDGEFOCUS_BOARD_PASS` | 板端 SSH 密码 |
| `EDGEFOCUS_BUILD_PASS` | 交叉编译机 SSH 密码 |
| `EDGEFOCUS_WIFI_SSID` / `EDGEFOCUS_WIFI_PASSWORD` | 板端 Wi-Fi |

示例：

```powershell
$env:EDGEFOCUS_BOARD_IP='192.168.10.210'
$env:EDGEFOCUS_DEPLOY_VISION_MODE='real'
python deploy/deploy_integration_test.py
```

## 文档

- [`docs/项目介绍和代码结构说明_代码接手.md`](docs/项目介绍和代码结构说明_代码接手.md)
- [`docs/EdgeFocus_比赛评委技术文档_详细版.md`](docs/EdgeFocus_比赛评委技术文档_详细版.md)
- [`docs/20260703封存文档/`](docs/20260703封存文档/) — 协议、板端环境、测试指南与开发入口
- 跨端协议：[`../../docs/protocols/`](../../docs/protocols/)
