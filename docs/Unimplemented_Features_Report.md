# EdgeFocus 占位与未实现功能 (Placeholders & Mocks) 排查报告

经过对当前代码库的排查，发现系统核心架构已经跑通，但在策略执行层和前端 UI 渲染层还存在部分占位符（Placeholder）、硬编码（Hardcode）或 Mock 逻辑。主要集中在以下几个方面：

## 1. 策略层 (Policy Layer) 占位
**文件：`focus_app/policy/ReminderPolicy.cpp`**
*   **未调用的 API (`processState`)**：`ReminderPolicy::processState()` 包含了完备的根据状态（如低头、玩手机）决策干预等级和冷却时间的逻辑。但是，在线程的 `runOnce()` 主循环中，并没有去获取 `SystemStateStore` 的状态并调用 `processState()`！
*   **现状**：`runOnce()` 内部写了注释 `// Placeholder: W06 will add real state-driven reminder logic.`，目前它仅仅每 30 秒清空一下遗留的提醒。这意味着**当前系统实际上不会自动触发任何屏幕闪烁或手环震动提醒**。

## 2. 物理外设与电源管理 (Physical Interaction & Power Management)
- **硬件集成 (Hardware Integration)**: 手环与主机的可拆卸物理设计（如磁吸充电座）尚未定型。
- **蓝牙守护进程按需激活 (BLE Daemon Activation Strategy)**: 目前 BLE 连接通过 UI SettingsPage 的 Connect/Disconnect 按钮手动控制。基于未来的物理设计，当手环吸附在主机上充电时，应当保持休眠；只有在手环被**物理取下**（可能通过霍尔传感器或者接触引脚检测）时，才按需拉起并激活蓝牙扫描连接。届时需将 UI 上的 Connect/Disconnect 功能迁移到物理分离检测逻辑上，UI 上仅保留连接状态显示和一个 Connect 按钮（实际作用为强行扫描连接，用于调试或异常恢复）。此逻辑需要预留到后续的硬件整合计划中。
- **BLE 连接稳定性问题 (Known Issue)**: 当前守护进程 `ble_daemon.sh` 通过 `bluetoothctl` 管理连接，存在手环连接后立刻断开（reason 1）的间歇性问题，导致 UI 可能短暂显示 "Connected" 后变为 "Connecting..."。根因可能是手环固件或 BLE 连接参数不兼容，待手环固件调试后解决。
- **云台自动校准 (Gimbal Auto-Calibration)**: 在 `CALIBRATING` 阶段，系统应利用视觉 AI 检测用户在画面中的位置，自动发送 PTZ (云台平移-俯仰-变焦) 指令来调整云台角度，而非完全依赖用户手动调节。
- **BLE 辅助连网与远程同步 (Auxiliary Remote Syncing)**: 最低优先级。通过手机端蓝牙（BLE）辅助开发板进行局域网配网，或者直接通过 BLE 代理网络请求，用于设备连接外网进行学习数据的云端同步以及远程监控画面查看。

## 3. 视觉模型升级 (AI_V2.0)
- **面部与视线追踪模型**: 当前的 `edgefocus_pose_v1_1.nb` 仅基于身体关键点（双眼、鼻子、肩膀）判断人体在位、低头、趴下等大动作，缺乏对面部特征和视线落点的精细追踪。在后续的 **AI_V2.0** 升级中，需要增加引入专用的面部 6-DoF 姿态（Yaw/Pitch/Roll）或视线追踪子模型，并将其融合进 `PoseModelAdapter.cpp`，以精准判定用户的“东张西望”和“发呆”等细微状态。

## 3. 前端 UI 层 (StatusPage / StatsPage) 数据缺失与硬编码
前端页面中有很多视觉设计需要后端传递数据，但由于 `SystemSnapshot`（系统状态快照）未提供对应字段，导致前端使用了大量假数据。

**文件：`focus_app/ui/StatusPage.cpp`**
*   **今日目标进度 (Today's Goal)**：由于 `SystemStateStore.today` 结构体中缺少 `goal_seconds`（目标时长），UI 代码中硬编码写死了 `"2h 45m"` 并在进度条中写死了 `0.3f` (30%) 的固定进度。

**文件：`focus_app/ui/StatsPage.cpp`**
*   **统计图表 (Bar Chart)**：中间的 5 根柱状图完全由硬编码的高度数组渲染 `int heights[] = {30, 50, 20, 60, 45};`，系统快照尚未提供历史几天的趋势数据。
*   **空数据兜底 (Mock Fallback)**：当今天的真实专注时长为 0 时（也就是系统刚开机还没开始统计），页面会强行显示假数据："总专注时长 4h 12m"，"效率 88%"，"分心次数 12 次" 用于视觉占位。

## 3. 视觉输入层 (Vision Layer) Mock 模式
**文件：`focus_app/camera/CameraPipeline.cpp` & `VisionEngine.cpp`**
*   **摄像头旁路**：目前由于 CMake 未开启 `EDGEFOCUS_WITH_OPENCV`，系统实际上没有真正打开 Linux V4L2 摄像头。
*   **现状**：底层一直在运行 `MockVisionSource`，所以日志里看到的专注状态流转，实际上是系统在自发产生的 Mock 事件，而非真实的图像推理。

## 总结与建议改进点
1. **打通提醒链路**：补全 `ReminderPolicy::runOnce()`，让它定时拉取 `FocusState` 并调用 `processState()`，从而真正激活物理设备提醒。
2. **扩充系统快照结构**：在 `TodayStats` 中加入 `goal_seconds` 和最近 5 次的专注图表数据，让前端 UI 能展现真实数据而不是硬编码。
3. **真实视觉接入**：在开发板部署 OpenCV，并连接真实 USB/CSI 摄像头替换掉当前的 `MockVisionSource`。
