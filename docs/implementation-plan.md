# EdgeFocus App 后续功能实现计划

> **For agentic workers:** 使用 subagent-driven-development 或 executing-plans 逐任务执行。

**Goal:** 完成 UI 清理、统计页数据丰富、BLE 设备绑定、数据同步逻辑修复

**Architecture:** 分 7 个独立任务，每个任务可独立编译测试

**Tech Stack:** Kotlin, Jetpack Compose, Room, BLE, Hilt

---

## Task 0: UI 小修复（快速）

**问题：** 设置项命名不清、Break 功能多余、模拟回复时长 bug、EdgeFocus 前台误通知。

**Files:**
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/settings/SettingsScreen.kt` — 删除 Short/Long Break 滑块，重命名 Focus
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/SettingsViewModel.kt` — 删除 break 相关 update 方法
- Modify: `app/src/main/java/com/edgefocus/app/data/local/entity/Settings.kt` — 删除 shortBreakDuration, longBreakDuration, sessionsBeforeLongBreak
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/HomeViewModel.kt` — 删除 break 逻辑
- Modify: `app/src/main/java/com/edgefocus/app/data/TimerStateHolder.kt` — 删除 startBreak
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BleManager.kt` — 修复模拟回复时长
- Modify: `app/src/main/java/com/edgefocus/app/service/AppMonitorService.kt` — 调试 EdgeFocus 前台误通知

- [ ] **Step 1: 删除 Break 相关代码**

Settings.kt 删除 `shortBreakDuration`, `longBreakDuration`, `sessionsBeforeLongBreak` 字段。
SettingsViewModel 删除 `updateShortBreakDuration`, `updateLongBreakDuration`。
SettingsScreen 删除 Short Break 和 Long Break 滑块。
HomeViewModel 删除 `shortBreakMinutes`, `longBreakMinutes`, `sessionsBeforeLongBreak` 和 `handleSessionFinished` 中的 break 逻辑。
TimerStateHolder 删除 `startBreak()`。

- [ ] **Step 2: 重命名 Focus 滑块**

SettingsScreen 中 "专注时长" 改为 "单次专注时长（分钟）"，更明确。

- [ ] **Step 3: 修复板端已在会话中的模拟回复时长**

BleManager.getCurrentState() 中，`simulateBoardInSession` 模式下剩余时间应基于 `simDurationMs` 计算（已修复，elapsed = duration * 0.3）。

- [ ] **Step 4: 调试 EdgeFocus 前台误通知**

在 AppMonitorService 的 `checkDistractionNotifications` 中，当 `foregroundPackage == packageName` 时增加日志确认。如果确认是 `getForegroundApp` 返回值问题，检查 `ACTIVITY_STOPPED` 事件是否干扰。

- [ ] **Step 5: 编译测试**

- [ ] **Step 6: Commit**

---

## Task 1: 统计页数据丰富

**问题：** 统计页数据来源单一，缺少从板端获取的统计数据。

**Files:**
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BleManager.kt` — 解析 get_device_status / get_today_summary 响应
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/DashboardViewModel.kt` — 添加板端数据字段
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/dashboard/DashboardScreen.kt` — 显示更多数据

- [ ] **Step 1: 定义板端数据模型**

在 `BleModels.kt` 中添加数据类（参考板端 `get_console_data` 返回结构）。

- [ ] **Step 2: 在 BleManager 中添加带超时的板端数据查询方法**

- [ ] **Step 3: DashboardViewModel 增加板端数据轮询**

- [ ] **Step 4: DashboardScreen 显示板端信息**

- [ ] **Step 5: 手动停止时也保存会话记录**

修改 `HomeViewModel.stopTimer()`：如果会话正在运行中（非 IDLE），调用 `handleSessionFinished()` 保存部分数据。

- [ ] **Step 6: 编译测试**

- [ ] **Step 7: Commit**

---

## Task 2: BLE 设备绑定（MAC/名称匹配）

**问题：** 当前 BLE 连接不区分设备，多个同名设备会连错。

**Files:**
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BleManager.kt` — 添加 MAC 过滤 + 扫描结果列表
- Modify: `app/src/main/java/com/edgefocus/app/data/local/entity/Settings.kt` — 添加 `edgeFocusMacAddress` 字段
- Create: `app/src/main/java/com/edgefocus/app/presentation/ui/settings/BleConnectScreen.kt` — BLE 扫描连接页面
- Modify: `app/src/main/java/com/edgefocus/app/presentation/navigation/NavigationGraph.kt` — 添加路由
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/home/HomeScreen.kt` — 蓝牙图标点击跳转连接页

- [ ] **Step 1: Settings 添加 macAddress 字段**

- [ ] **Step 2: BleManager 添加 MAC 过滤扫描**

- [ ] **Step 3: 创建 BleConnectScreen**

- [ ] **Step 4: HomeScreen 蓝牙图标跳转**

- [ ] **Step 5: NavigationGraph 添加路由**

- [ ] **Step 6: 编译测试**

- [ ] **Step 7: Commit**

---

## Task 3: 数据同步逻辑修复

**问题：** Local 模式会话不记录到统计；EdgeFocus 模式数据未同步给板端。

**Files:**
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/HomeViewModel.kt` — 修复会话保存
- Modify: `app/src/main/java/com/edgefocus/app/data/TimerStateHolder.kt` — syncSessionToBoard 改进

- [ ] **Step 1: 修复手动停止不保存会话**

- [ ] **Step 2: 会话记录填充真实数据（distractionCount, reminderCount）**

- [ ] **Step 3: Local 模式会话同步给板端**

- [ ] **Step 4: 编译测试**

- [ ] **Step 5: Commit**

---

## Task 4: EdgeFocus 长时间停留误通知修复

**问题：** 用户在 EdgeFocus 内停留超过 5 分钟后，`getForegroundApp` 返回 null，触发后台通知。

**Files:**
- Modify: `app/src/main/java/com/edgefocus/app/service/AppMonitorService.kt` — 添加缓存判断

- [ ] **Step 1: 添加 lastKnownForegroundPackage 缓存**

在 `AppMonitorService` 中记录上次检测到的前台包名。当 `getForegroundApp` 返回 null 且 `lastKnownForegroundPackage == packageName`（EdgeFocus），视为仍在前台，不触发通知。

- [ ] **Step 2: 编译测试**

- [ ] **Step 3: Commit**

---

## Task 5: 沟通文档定稿 + 板端待确认事项

**Files:**
- Create: `docs/board-communication.md` — 已创建，需补充板端确认后的信息

- [ ] **Step 1: 将文档发送给板端团队**

需要板端确认：
1. `push_timer_complete` 中 `distraction_count` / `reminder_count` 的具体含义
2. `get_device_status` 返回字段
3. `get_today_summary` 返回字段
4. `get_console_data` 完整返回结构
5. 所有命令响应是否统一为 JSON 格式

- [ ] **Step 2: 根据板端回复更新文档**
