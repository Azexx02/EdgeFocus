# EdgeFocusApp - Agent Guidelines

## Project Overview

EdgeFocusApp 是一个 Android 专注辅助应用，配合 EdgeFocus 硬件设备使用。核心功能是番茄钟计时器，通过 BLE 与开发板同步。

**设计原则：**
- EdgeFocus 设备是核心，可以独立完成核心功能
- 手机 App 是遥控器 + 替代板载屏幕的控制台
- 同时支持不使用设备的本地模式

**技术栈：**
- Kotlin + Jetpack Compose
- Hilt (依赖注入)
- Room (本地数据库)
- Android BLE API
- MVVM 架构

## Project Structure

```
app/src/main/java/com/edgefocus/app/
├── data/
│   ├── local/          # Room 数据库、DAO、实体
│   ├── remote/         # BLE 通信 (BleManager, BleCommand)
│   ├── repository/     # 数据仓库实现
│   ├── TimerStateHolder.kt  # 共享计时器状态（核心）
│   └── SessionMode.kt  # 会话模式枚举
├── domain/
│   ├── model/          # 领域模型
│   └── repository/     # 仓库接口
├── presentation/
│   ├── ui/             # Compose UI 组件
│   ├── viewmodel/      # ViewModel
│   └── navigation/     # 导航
├── service/            # 前台服务
├── di/                 # Hilt 模块
└── util/               # 工具类
```

## Key Design Decisions

### TimerStateHolder 作为单一数据源

- HomeViewModel 和 ControlPanelViewModel 都从 TimerStateHolder 读取状态
- HomeViewModel 负责计时器逻辑，更新 TimerStateHolder
- ControlPanelViewModel 只读取状态，用于 UI 显示

### 两种会话模式

1. **EdgeFocus 模式** (`SessionMode.EDGEFOCUS`)
   - 必须连接设备才能启动
   - 先通知板端开始，等待响应后跟随板端时间
   - 本地倒计时作为保底，防止蓝牙断连
   - 恢复会话时也先通知板端

2. **本地模式** (`SessionMode.LOCAL`)
   - 不需要连接设备
   - 纯本地倒计时
   - 结束后可选择同步记录给设备

### 前台应用检测

- 使用 `UsageStatsManager.queryEvents()` (60秒窗口 + 5分钟兜底)
- 追踪 RESUMED 和 PAUSED 事件判断应用是否在前台
- 不过滤 EdgeFocus 自身（由 AppMonitorService 判断）

### 通知策略

| 场景 | 行为 |
|------|------|
| EdgeFocus 在前台 | 不通知 |
| 强干扰应用 | 每秒提醒 |
| 弱干扰应用 | 3秒后首次，之后每分钟 |
| 学习软件 | 不提醒 |
| 未分类应用 | 每10秒提醒 |
| 锁屏/桌面 | 后台通知，每10秒 |

### 干扰应用分级

- 0: 学习软件（不提醒）
- 1: 弱干扰源（社交媒体）
- 2: 强干扰源（短视频、游戏）
- 3: 未分类（默认）

## Coding Conventions

### Compose UI
- 使用 Material 3 组件
- 状态提升模式：ViewModel 持有状态，Composable 只负责显示
- 使用 `hiltViewModel()` 注入 ViewModel

### BLE 通信
- BleManager 是 @Singleton，通过 Hilt 注入
- BLE 命令使用 JSON 格式
- 所有 BLE 操作都需要 `@SuppressLint("MissingPermission")`

### 数据库
- Room 实体使用 @Entity 注解
- DAO 使用 Flow 返回响应式数据
- 数据库版本迁移使用 fallbackToDestructiveMigration()

## Build & Run

```bash
# 编译
./gradlew assembleDebug

# 安装到设备
./gradlew installDebug
```

## Testing

```bash
# 单元测试
./gradlew test

# UI 测试
./gradlew connectedAndroidTest
```

## Known Issues & TODO

1. **锁屏通知** - 当前锁屏时会触发后台通知，可以考虑移除
2. **单元测试** - 目前没有单元测试，需要添加
3. **错误处理** - BLE 通信错误处理可以更完善
4. **UI 适配** - 控制面板横屏 UI 需要在不同设备上测试

## Recent Changes

- 修复前台应用检测：使用 60秒窗口 + 状态追踪解决静止使用时误报
- 修复通知逻辑：EdgeFocus 在前台时不触发通知
- BLE 交互策略：基于 useBoard 设置而非连接状态
- 会话启动逻辑：EdgeFocus 模式先通知板端，等待响应后跟随板端时间

## API Contract

详见 `C:\Users\Aze\Desktop\APP_To_Board\require.md`（手机端与板端需求交流）
