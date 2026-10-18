# EdgeFocus 模块补全计划

## ⚠️ 锁定文件（禁止修改）

以下文档已被手机端/手环端团队锁死开发，**禁止修改**：
- `docs/Mobile_API_Contract_v2.md` — 手机端 API 契约
- `docs/BLE_Signal_Protocol.md` — 手环 BLE 协议
- `docs/BLE_API.md` — 手环 API 文档

## 策略

每个模块独立完成、独立可测，最后拼装。每个阶段包含：**目标 → 改动 → 验证 → 完成标准**。

---

## Phase 1: Storage 层补全（数据基础）

**目标**：让 `SQLiteStore` + `ReportGenerator` 能完整支撑 session 生命周期的数据读写。

### 1.1 SQLiteStore 补查询接口

当前 `SQLiteStore` 只有 `logEvent()` 写入，没有任何查询方法。`ReportGenerator` 自己开独立 DB 连接做查询，这是架构问题。

**改动：**
- `SQLiteStore.h` 新增 `queryEvents(session_id)` 方法
- `SQLiteStore.cpp` 实现：从 `event` 表按 `session_id` 查询所有事件，返回 `std::vector<StorageEvent>`

**验证：** 单元测试 `test_storage.cpp` 新增查询用例

### 1.2 ReportGenerator 接入 SQLiteStore

当前 `ReportGenerator` 构造时自己 `sqlite3_open()`，与 `SQLiteStore` 的 DB 连接完全独立。

**改动：**
- `ReportGenerator` 改为接收 `SQLiteStore&` 引用（或接受 `sqlite3*`）
- `generateReport()` 内部通过 `SQLiteStore::queryEvents()` 获取数据，而非自己开连接
- 删除 `ReportGenerator` 自带的 `db_` 成员

**验证：** `test_storage.cpp` 测试 session 结束后 `generateReport()` 返回正确数据

### 1.3 StorageEvent::timestamp 类型修复

`StorageEvent.h:16` 的 `timestamp` 是 `int`，会 2038 溢出。

**改动：** 改为 `int64_t`

**验证：** 编译通过 + 已有测试通过

### 1.4 SessionInfo 补 target_duration_ms

当前 `SessionInfo` 无目标时长字段，番茄钟功能无法工作。

**改动：**
- `SystemStateStore.h` 中 `SessionInfo` 新增 `int64_t target_duration_ms = 0;`
- `BehaviorEngine::runOnce()` 中计算 `remaining_ms = target_duration_ms - elapsed_ms`
- `remaining_ms <= 0` 且 `target_duration_ms > 0` 时触发 `push_timer_complete`

**验证：** `session_control` start 传入 `target_duration_min=1` → 60 秒后收到 `push_timer_complete`

### 1.5 SessionReport 扩展字段

项目书要求复盘报告包含"最易分心时间段"、"各类提醒的生效情况"。当前 `SessionReport` 只有 4 个基础字段。

**改动：**
- `ReportGenerator.h` 中 `SessionReport` 新增：
  ```cpp
  int64_t longest_focus_streak_ms = 0;  // 最长连续专注时长
  int phone_distraction_count = 0;       // 手机干扰次数
  ```
- `ReportGenerator::generateReport()` 从 SQLite 查询中聚合这些字段

**验证：** session 结束后 `get_last_report` 返回扩展字段

---

## Phase 2: Policy → Device 管线接通（告警能触发）

**目标**：`ReminderPolicy` 产生的 `InterventionCommand` 能到达 `DeviceManager` 并执行。

### 2.1 ReminderPolicy 持有 DeviceManager 引用

当前 `ReminderPolicy::runOnce()` 生成 cmd 后只写 store，不调用 `DeviceManager::issueCommand()`。

**改动：**
- `ReminderPolicy` 构造函数新增 `DeviceManager&` 参数
- `ReminderPolicy.h` 新增成员 `DeviceManager& device_mgr_`
- `runOnce()` 中 `cmd.has_value()` 时调用 `device_mgr_.issueCommand(*cmd)`
- `main.cpp` 中构造 `ReminderPolicy` 时传入 `DeviceManager` 实例
- **修复**：`ReminderPolicy.cpp:106` 和 `BehaviorEngine.cpp:78` 的 `session_id` 硬编码问题，改为从 `SessionInfo` 获取

**验证：** 编译通过 + `test_reminder_policy.cpp` 新增 mock DeviceManager 测试

### 2.2 ScreenController::flashScreen() 实现

当前是空壳只打日志。

**改动：**
- `ScreenController::flashScreen()` 通过 `FrameBuffer` 或 GPIO 实现屏幕闪烁
- 方案 A：反转背光 GPIO（`PD8`）多次
- 方案 B：通过 `FrameBuffer` 交替刷全白/全黑帧

**验证：** 板端手动测试闪烁效果

### 2.3 BleBandController::sendAlert() level 参数生效

当前 `(void)level;` 忽略参数。

**改动：** 根据 `level` 发送不同强度的震动指令（如 `0x01` 轻震, `0x02` 重震）

**验证：** 板端手动测试手环震动

### 2.4 SettingsPage → ModeManager 接通

当前设置页切换模式只写 store，不更新 `ModeManager`，策略层永远读到 NORMAL。

**改动：**
- `SettingsPage` 构造时接收 `ModeManager&` 引用
- 模式切换时调用 `mode_mgr_.setMode(new_mode)` 同时写 store
- `UIManager::init()` 中将 `ReminderPolicy::getModeManager()` 传给 `SettingsPage`

**验证：** 切换模式后 `ReminderPolicy` 读到正确 mode

### 2.5 FocusState 手机干扰细分

当前 `FocusState` 已细分为 `PHONE_IN_USE`、`PHONE_IN_LEARNING`、`PHONE_IN_PLAYING`。

**改动：**
- `FocusState.h` 新增枚举值：
  ```cpp
  PHONE_IN_USE = 3,      // 弱干扰/未分类（聊天、后台）
  PHONE_IN_LEARNING = 8, // 学习类 App（不干预）
  PHONE_IN_PLAYING = 9,  // 强干扰（游戏、短视频）
  ```
- `EventDetector` 根据 `distraction_level` 设置不同状态：
  - `level=0`（学习）→ `PHONE_IN_LEARNING`
  - `level=1`（弱干扰）→ `PHONE_IN_USE`
  - `level=2`（强干扰）→ `PHONE_IN_PLAYING`
  - `level=3`（未分类）→ `PHONE_IN_USE`
- `ReminderPolicy` 干预映射：
  - `PHONE_IN_LEARNING` → 不干预
  - `PHONE_IN_USE` / `PHONE_IN_PLAYING` → 行为/策略综合风险上下文决定 `CommandLevel`
- `FocusStateMachine::determineCandidateState()` 更新优先级：
  ```
  LOW_CONFIDENCE > AWAY > PHONE_IN_PLAYING > PHONE_IN_USE > PHONE_IN_LEARNING > SLEEPY_OR_SLUMPED > LOOKING_AWAY > FOCUSED
  ```

**验证：** 模拟不同 distraction_level → 触发对应 FocusState → 触发对应 CommandLevel

---

## Phase 3: UI 真实数据绑定（去掉假数据）

**目标**：所有 UI 页面显示真实数据，不再有硬编码 fallback。

### 3.1 TodayStats 聚合器

当前 `TodayStats` 从未被写入，StatsPage 永远显示零。

**改动：**
- 新增 `TodayStatsAggregator` 类（或在 `BehaviorEngine` 中增加逻辑）
- 每次 `FocusState` 变化时累加 `focus_seconds`、`events`
- Session 结束时调用 `store_.updateTodayStats()`
- 每天零点重置统计
- **扩展**：`TodayStats` 新增 `phone_distraction_count` 字段（手机干扰次数）

**验证：** 运行一个 session 后 StatsPage 显示真实数据

### 3.2 Session 结束时生成 Report

当前 `StatusPage` PAUSE 时跳转 ReviewPage，但从未调用 `ReportGenerator`。

**改动：**
- `StatusPage` 构造时接收 `ReportGenerator&` 和 `SQLiteStore&`
- PAUSE 按钮处理中：先调用 `report_generator_.generateReport(session_id)` → `store_.updateLastReport(report)` → 再跳转 ReviewPage
- ReviewPage 删除所有 `== 0` 的 fallback 假数据

**验证：** Session 结束后 ReviewPage 显示真实分数、时长、分心次数

### 3.3 StatsPage 假数据清除

**改动：**
- 删除 `focus_str = "4h 12m"` 硬编码（line 20）
- 删除 `int heights[] = {30, 50, 20, 60, 45}` 柱状图假数据（line 35）→ 改为从 `TodayStats` 或 SQLite 查询最近 5 个 session 的 focus 时长
- 删除 `eff` 默认 88（line 50）→ 真实计算
- 删除 `events` 默认 12（line 61）→ 真实值

**验证：** StatsPage 显示真实统计

### 3.4 UIManager::renderGlobalHeader() 实现

当前函数体为空，状态栏从不渲染。

**改动：**
- 实现顶部 40px 状态栏：左侧系统模式图标（N/S/H），中间摄像头状态，右侧电池百分比
- 从 `snap.device` 读取数据

**验证：** 屏幕顶部显示真实状态信息

### 3.5 StatusPage 环形进度条真实化

当前 `progress = 0.75f` 硬编码（line 131）。

**改动：** 改为 `elapsed_ms / goal_ms` 计算真实进度

**验证：** 环形进度条随时间增长

---

## Phase 4: Vision/Behavior 逻辑修复 + 补全

**目标**：修复视觉层精度问题，补全分析层 4 个逻辑缺陷，接通手机检测和 RiskScorer。

---

### 4.1 EventDetector 接通手机检测（仅 BLE 路径）

**问题：** `EventDetector::detect()` 依赖 `injectPhoneSignal()` 外部注入，但整个代码库从未调用此函数。`PHONE_IN_USE` 永远无法触发。

**改动（仅 BLE 路径，放弃视觉检测）：**
- 保留 `injectPhoneSignal()` 接口，由 `BleGattServer` 收到 `phone_sync` 时调用
- `injectPhoneSignal()` 根据 `distraction_level` 设置不同状态：
  - `level=0`（学习）→ `FocusState::PHONE_IN_LEARNING`
  - `level=1`（弱干扰）→ `FocusState::PHONE_IN_USE`
  - `level=2`（强干扰）→ `FocusState::PHONE_IN_PLAYING`
  - `level=3`（未分类）→ `FocusState::PHONE_IN_USE`
- **不读取** `VisionResult::phone` 字段（视觉检测方向已放弃）

**验证：** `test_behavior_fsm.cpp` 模拟 `injectPhoneSignal(distraction_level=2)` → 状态机输出 `PHONE_IN_PLAYING`

### 4.2 EventDetector 补全 LOOKING_AWAY 检测

**问题：** `BehaviorConfig.looking_away_confirm_ms = 8000` 已定义，`FocusStateMachine` 有 `LOOKING_AWAY` 候选状态，但 `EventDetector` 中没有代码设置 `is_looking_away = true`。人在画面中但视线偏离时不会产生事件。

**改动：**
- `EventDetector::detect()` 中新增逻辑（在 away 检测之后、sleepy 检测之前）：
  ```cpp
  // Looking away: person present but head yaw > 30° for confirming duration
  int64_t looking_away_dur = getContinuousDuration(win, [](const VisionResult& r) {
      return r.isPresent() && r.head_pose.valid 
          && (std::abs(r.head_pose.value.yaw) > 30.0f);
  });
  if (looking_away_dur >= config.looking_away_confirm_ms) {
      event.is_looking_away = true;
  }
  ```
- `getContinuousDuration()` 需要改为从窗口中取最新帧（当前逻辑正确，从尾部向前扫描）

**验证：** `test_behavior_fsm.cpp` 模拟连续 8 秒 yaw>30° 的 VisionResult → 触发 LOOKING_AWAY → Policy 生成 LEVEL_1 提醒

### 4.3 FocusStateMachine 校准质量门控

**问题：** `FocusStateMachine.cpp:25-31` 中 CALIBRATING → FOCUSED 转换只检查"有 1 秒数据"，不检查帧质量。摄像头被遮挡时也会进入 FOCUSED。

**改动：**
- `processFrame()` 的 CALIBRATING 分支增加质量检查：
  ```cpp
  if (current_state_ == FocusState::CALIBRATING) {
      if (!win30s_.getItems().empty() && 
          (frame.timestamp_ms - win30s_.getItems().front().timestamp_ms) >= 1000) {
          // Require at least some frames with good quality before transitioning
          float good_ratio = win30s_.getRatioCondition([](const VisionResult& r) {
              return r.frame_quality.valid && r.frame_quality.level == FrameQualityLevel::GOOD;
          });
          if (good_ratio >= 0.3f) {  // At least 30% good frames in calibration window
              current_state_ = FocusState::FOCUSED;
          }
      }
      return;
  }
  ```

**验证：** 模拟全黑帧 calibration → 不应转 FOCUSED；正常帧 calibration → 正常转 FOCUSED

### 4.4 head_down_score 低可见度静默失效修复

**问题：** `PoseModelAdapter.cpp:209` 当 `head_visible = false`（鼻子或肩膀关键点不可见）时，`head_state` 保持 NORMAL，低头检测静默失效。应触发 LOW_CONFIDENCE 而非默认"正常"。

**改动：**
- `PoseModelAdapter.cpp:226-233` 的 else 分支修改：
  ```cpp
  } else {
      out.head_down_score = 0.0f;
      out.head_state = HeadState::NORMAL;
      out.head_pose = {HeadPose{}, 0.0f, false};
      out.posture = {PostureType::NOT_VISIBLE, 0.0f, false};
      out.reason_code = ReasonCode::LOW_CONFIDENCE;  // Already set, but ensure downstream handles it
  }
  ```
- `InferenceScheduler::buildResult()` 中，当 `inference.reason_code == ReasonCode::LOW_CONFIDENCE` 且 `inference.person_detected == true` 时，将 `result.presence` 设为 `UNCERTAIN` 而非 `PRESENT`，确保 EventDetector 的 `is_low_confidence` 能捕获此场景

**验证：** 模拟 person_detected=true 但 head_visible=false → VisionResult 输出 UNCERTAIN presence → EventDetector 输出 LOW_CONFIDENCE

### 4.5 CpuFallbackPoseAdapter head_down_score 精度修复

**问题：** `PoseModelAdapter.cpp:204` 中 `visible_count / 5.0f` 虽然碰巧因为浮点乘法顺序不会出大错，但 `presence_conf` 计算语义不清晰，且 `head_down_score` 的 pitch 阈值 `-18.0f` 与 `dominantFromPitch()` 的 `-20.0f` 不一致。

**改动：**
- `CpuFallbackPoseAdapter::infer()` 中：
  - `out.presence_conf = clamp01(out.person_confidence * (static_cast<float>(visible_count) / 5.0f));` — 显式 cast 确保浮点除法
  - 统一 pitch 阈值：`dominantFromPitch()` 和 `head_down_score` 判断都用 `-20.0f`

**验证：** 编译通过 + `test_vision_result.cpp` 验证 presence_conf 边界值

### 4.6 RiskScorer 接入 FocusStateMachine

**问题：** `RiskScorer scorer_` 成员已声明但从不调用。

**改动：**
- `FocusStateMachine::processFrame()` 末尾调用：
  ```cpp
  FocusEvent event = detector_.detect(win30s_, config_);
  int risk = scorer_.calculateScore(event);
  // risk 可用于日志或未来扩展决策
  ```
- `RiskScorer::calculateScore()` 参数改为接收 `const FocusEvent&`（当前签名已经是）
- 在 `FocusStateMachine` 中新增 `int getRiskScore() const` 访问器

**验证：** `test_behavior_fsm.cpp` 验证 risk score 随事件变化

---

## Phase 5: Network 层（板端 BLE GATT Server）

**目标**：实现板端 BLE GATT Server，按 `docs/Mobile_API_Contract_v2.md` 契约响应手机端命令并推送事件。

### 5.1 新增 BleGattServer 模块骨架

**改动：**
- 新增 `network/BleGattServer.h` 和 `network/BleGattServer.cpp`
- 继承 `Module` 基类，独立线程运行
- BlueZ D-Bus API 实现 GATT Server
- Service UUID `0000FF00-...`，TX `0xFF01`（Write），RX `0xFF02`（Read/Notify）
- 构造函数接收：`SystemStateStore&`、`SQLiteStore&`、`ReminderPolicy&`、`ModeManager&`
- TX Write 回调：收到 JSON → `handleCommand(json)` → 组装响应 → Notify RX

**验证：** nRF Connect 扫描到开发板 BLE，连接后写 TX 收到响应

### 5.2 逐条实现 API 命令

每个 cmd 对应 `BleGattServer::handleCommand()` 中的一个分支：

**5.2.1 `get_device_status`**
- 读 `store_.snapshot().device` + `.session.active`
- 返回 `battery_percent`, `system_mode`, `camera_ok`, `bracelet_connected`, `is_session_active`

**5.2.2 `get_current_state`**
- 读 `store_.snapshot().session` + `focus_state`
- 返回 `focus_state`, `elapsed_ms`, `target_duration_ms`, `remaining_ms`, `distraction_count`, `reminder_count`, `calibration_completed`

**5.2.3 `get_today_summary`**
- 读 `store_.snapshot().today`
- 返回 `total_seconds`, `focus_seconds`, `sessions`, `events`
- **依赖**：Phase 3.1 TodayStats 聚合器

**5.2.4 `get_last_report`**
- 读 `store_.snapshot().last_report`
- 返回 `total_duration`, `focus_duration`, `distraction_count`, `reminder_count`
- 无报告时返回 `data: null`
- **依赖**：Phase 1.2 ReportGenerator 接入

**5.2.5 `session_control`**
- `start`：构造 `SessionInfo{active=true, calibrating=true}` → `store_.updateSession()` + `notifySessionChange()`
  - 若 `target_duration_min` > 0，设置 `session.target_duration_ms = target_duration_min * 60000`
- `pause`：`SessionInfo{active=false}` → `store_.updateSession()` + `notifySessionChange()`
- `end`：pause + `report_generator_.generateReport()` → `store_.updateLastReport()`
- 前置检查：start 时确认无活跃会话，pause/end 时确认有活跃会话
- **依赖**：需从 `StatusPage::onTouch()` 抽出为独立可调用逻辑

**5.2.6 `update_preferences`**
- 调用 `mode_mgr_.setMode(static_cast<SystemMode>(system_mode))`
- 更新 `store_.updateDeviceStatus()`
- 返回生效后的 `system_mode`

**5.2.7 `reminder_ack`**
- 调用 `reminder_policy_.onUserAck(now_ms)`
- 返回 `cooling_ms`（当前冷却时间）

**5.2.8 `phone_sync`**
- 将 `distraction_level` 和 `is_in_use` 转为 `PhoneUsageSignal` 传给 `EventDetector::injectPhoneSignal()`
- `distraction_level` 映射到不同 FocusState：
  - `0`（学习）→ `PHONE_IN_LEARNING`
  - `1`（弱干扰）→ `PHONE_IN_USE`
  - `2`（强干扰）→ `PHONE_IN_PLAYING`
  - `3`（未分类）→ `PHONE_IN_USE`
- `is_in_use=false` → 清除手机干扰状态（恢复 FOCUSED 或其他状态）
- 返回处理后的 `focus_state` 和 `action_taken`
- **不依赖 VisionResult.phone**（视觉检测方向已放弃）
- **依赖**：Phase 4.1 EventDetector 接入

**5.2.9 `get_console_data`**
- 读 `store_.snapshot()` 全量数据
- 返回 `session`（含 `target_duration_ms`, `remaining_ms`）、`device`、`reminder`、`frame_quality`、`today`
- **依赖**：Phase 3.1 TodayStats 聚合器

**验证：** nRF Connect 逐条写入 cmd → 收到正确 JSON 响应

### 5.3 实现主动推送

**改动：**
- `SystemStateStore` 新增 `onChange(std::function<void(const SystemSnapshot&)>` 回调
- `BehaviorEngine` 状态切换时触发 `push_state` Notify
- `ReminderPolicy` 生成命令时触发 `push_alert` Notify
- `session_control` end 完成后触发 `push_session_end` Notify
- `BehaviorEngine` 检测到 `elapsed_ms >= target_duration_ms` 时触发 `push_timer_complete` Notify

**验证：** 手机端订阅 Notify → 板端状态变化时收到推送

### 5.4 main.cpp 注册

**改动：**
- 构造 `BleGattServer` 并注册到 `AppContext`
- 启动顺序：SQLiteStore → BleGattServer → Camera/Vision/Behavior/Policy/Device/UI

**验证：** 完整启动后手机端可连接并完成全部交互

---

## Phase 6: 死代码清理 + 文档同步

### 6.1 清理

- `CalibrationPage`：要么接入 UIManager 要么删除（当前 StatusPage 有自己的内联校准 UI）
- `focusStateEmoji()`：实现或删除
- `ReviewPage::formatDuration()`：实现或删除
- HOME 模式：UI 加入口或从代码中移除
- `EventDetector::injectPhoneSignal()` 和 `FocusStateMachine::injectPhoneSignal()`：Phase 4.1 已删除
- `docs/Mobile_App_API_Spec.md`：废弃，替换为 `docs/Mobile_API_Contract_v2.md`
- `docs/mobile_api_contract.md`：废弃，替换为 `docs/Mobile_API_Contract_v2.md`

### 6.2 文档同步

- `docs/API_Reference.md`：`StateVar<T>` → `ConfidenceValue<T>`
- `docs/API_Reference.md`：删除不存在的 `queryEvents()` 引用（Phase 1.1 实现后恢复）
- `ReportGenerator.cpp`：`std::cout` → `Logger`
- `docs/Modules_Overview.md`：更新 EventDetector 描述（从 VisionResult.phone 直接读取，非外部注入）
- `docs/Modules_Overview.md`：新增 BleGattServer 模块描述

---

## 拼装顺序

```
Phase 1 (Storage)  ──┐
                      │
Phase 2 (Policy→Dev) ─┤
                      ├──→ Phase 5 (Network) ──→ Phase 6 (清理+文档)
Phase 3 (UI 数据)  ───┤
                      │
Phase 4 (Vision/Bhv) ─┘
```

- Phase 1-4 可并行开发（无交叉依赖）
- Phase 5 依赖 Phase 1（Storage）和 Phase 2（Policy）完成
- Phase 6 在全部完成后统一清理

### 执行建议

1. **先跑测试**：`cd focus_app/build && ctest --output-on-failure` 确认当前基线
2. **Phase 优先级**：手机端团队在等 API，建议优先 Phase 1.2 + 3.1 + 4.1（解锁 `get_today_summary`, `get_last_report`, `phone_sync`）
3. **每完成一个改动**：立即跑测试，不要攒到最后
4. **修改 `SystemStateStore` 时**：注意所有模块都依赖它，改动后全量编译验证

---

## 每阶段完成标准

| Phase | 完成标准 |
|-------|---------|
| 1 | `ctest` 全部通过，`ReportGenerator` 通过 `SQLiteStore` 查询生成正确报告，`SessionInfo` 含 `target_duration_ms`，`SessionReport` 含扩展字段 |
| 2 | 模拟 FocusState=PHONE_IN_USE → DeviceManager 收到 cmd → 屏幕闪烁/手环震动 |
| 3 | 运行 session → StatsPage/ReviewPage 显示真实数据，无硬编码 |
| 4 | EventDetector 从 VisionResult.phone 读取手机信号 → PHONE_IN_USE 触发；LOOKING_AWAY 能被检测；校准阶段质量门控生效；低可见度不产生虚假 NORMAL |
| 5 | 手机端 nRF Connect 连接板端 BLE → 逐条写入 10 个 cmd 均收到正确 JSON 响应 → 订阅 Notify 后板端状态变化触发推送（含 `push_timer_complete`） |
| 6 | 无死代码，文档与代码一致，`Logger` 统一，旧文档已废弃 |

---

## 项目书功能覆盖情况

| 项目书功能 | 计划覆盖 | 说明 |
|-----------|---------|------|
| 6.1 实时专注状态识别 | ✅ Phase 4 | Vision+Behavior 已有 |
| 6.2 手机干扰检测 | ✅ Phase 4.1 | phone_sync API + EventDetector |
| 6.3 分级提醒机制 | ✅ Phase 2 | ReminderPolicy + DeviceManager |
| 6.4 静音模式 | ✅ | SystemMode::SILENT 已有 |
| 6.5 家庭模式家长监护 | ⏳ 跳过 | 优先级最低，当前不实现 |
| 6.8 复盘报告 | ✅ Phase 1.5 | SessionReport 扩展字段 |
| 7.x 主动学习 | ⚠️ 待定 | 优先级高，但 NanoEdge AI 是否使用待确认 |
| Home 基座 | ⏳ 硬件团队 | 已在开发中 |
| 云台自调节 | ⏳ 硬件团队 | 已在开发中 |
