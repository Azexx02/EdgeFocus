# EdgeFocus Android App

EdgeFocus 的手机客户端，是产品的**唯一主要交互渠道**。它负责发起与控制会话、上报手机原始用机状态、展示主机权威状态与报告，并通过 BLE 接收主机的提醒与 push。

## 技术栈

| 项目 | 版本 / 说明 |
| --- | --- |
| 语言 | Kotlin |
| UI | Jetpack Compose（Material 3） |
| 架构 | MVVM + Repository + Hilt |
| 本地存储 | Room（含迁移与 schema 导出） |
| 异步 | Kotlin Coroutines / Flow |
| 通信 | BLE GATT Client（JSON 命令 + 定长帧 push） |
| 构建 | Gradle Kotlin DSL、KSP、AGP |
| SDK | minSdk 26、targetSdk 34、compileSdk 34、JDK 17 |

## 目录

```text
app/src/main/java/com/edgefocus/app/
├── data/            # BLE、Room、偏好设置、会话缓存
├── domain/          # 会话模型、状态机、用例
└── presentation/    # Compose 页面、ViewModel、主题
app/schemas/         # Room schema
docs/                # 架构与迭代记录
```

## 主要页面

- 首页 / 设备连接
- 预检（PRECHECK）：云台构图、状态确认、一键跳过、故障降级提示
- 专注会话：计时、状态、提醒
- 报告 / 历史 / 反馈
- 设置：提醒渠道、静音、性能模式等

## 构建

```bash
# 1. 指定 Android SDK
echo "sdk.dir=C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk" > local.properties

# 2. 构建
./gradlew assembleDebug
```

Debug 包输出在 `app/build/outputs/apk/debug/`。BLE 需要真机测试；模拟器无蓝牙。

## 与主机联调

1. 主机以 `EdgeFocus` 为广播名开启 GATT Server。
2. App 连接后订阅 FF01（命令）与 FF02（push），通过 FF00 发送 JSON。
3. 字段级契约见 [`docs/protocols/EdgeFocus-最终API契约-App主机.md`](../docs/protocols/EdgeFocus-最终API契约-App主机.md)。
4. App 只上报 `未使用 / 学习 / 娱乐 / 其他`，不自行生成最终分神等级。

## 文档

- [`docs/architecture/edgefocus-app-architecture.md`](docs/architecture/edgefocus-app-architecture.md)
- [`docs/EdgeFocusApp_云台手动控制与BLE协议适配实施文档.md`](docs/EdgeFocusApp_云台手动控制与BLE协议适配实施文档.md)
- [`docs/plans/`](docs/plans/) / [`docs/specs/`](docs/specs/) — 迭代设计与实施记录
