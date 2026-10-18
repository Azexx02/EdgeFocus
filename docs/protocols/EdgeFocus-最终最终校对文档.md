# EdgeFocus 最终最终校对文档

> 日期：2026-06-30  
> 目的：校对旧总结、旧推进计划和代码现状之间的偏差，给出当前应采用的唯一口径。  
> 结论：当前项目应围绕“主机独立完成核心功能 + App/手环接入主链路”推进。协议冻结文件不直接修改，若未来接口变化必须走 P0 变更流程。

---

## 1. 校对基准

本次校对参考：

| 来源 | 用途 |
|---|---|
| `P0-协议冻结清单.md` | 协议和跨端字段的最高依据 |
| `doc/01-主体文档.md` 到 `doc/05-手环文档.md` | 旧最终对齐文档，需要按新事实校正理解 |
| `BoardCode/focus_app` 当前代码 | 主机实际实现依据 |
| 用户近期实测反馈 | UI 行为、手环封装状态、产品决策依据 |
| 本轮本地测试 | 主机代码验证依据 |

本轮主机本地验证结果：

```text
cmake --build build --config Debug: passed
ctest -C Debug --output-on-failure: 9/9 passed
test_integration.exe: 231/231 passed
```

---

## 2. 最重要校对结论

| 编号 | 旧风险口径 | 最终口径 | 状态 |
|---|---|---|---|
| C-01 | 当前优先推进云台/校准细节 | 当前不推进云台驱动、不改校准协议，只保留云台页和手动 skip | 已校正 |
| C-02 | 校准 placeholder 自动完成可作为产品口径 | start 后进入预检/云台页，用户手动 skip 后进入 focusing；自动跳过若出现按 bug 看待 | 已校正 |
| C-03 | App 校准页是当前最大缺口 | 当前先以主机独立闭环为主，App UI 后续回归，不阻塞主机核心功能 | 已校正 |
| C-04 | `PHONE_IN_USE` 固定 `LEVEL_2` | 手机状态只是行为输入，等级由 ReminderPolicy 根据上下文计算 | 已校正 |
| C-05 | `PHONE_IN_PLAYING` 固定 `LEVEL_3` | 同上，不固定等级 | 已校正 |
| C-06 | silent 模式会降级提醒 | silent 只筛选无声提醒渠道，不改变严重程度 | 已校正 |
| C-07 | App 可直接控制手环震动 | App 只连主机；手环只接收主机最终 Alert | 已冻结 |
| C-08 | 手环可 ACK/Notify/上报电量 | 当前没有，不做，手环已送封装 | 已冻结 |
| C-09 | Dashboard 重构是当前主线 | 不是；当前主线是 start -> behavior -> reminder -> report/history/feedback | 已校正 |
| C-10 | `phone_sync` 响应可直接驱动 UI | 只能作为 ACK/调试，UI 以 `push_state/get_current_state` 为准 | 已校正 |

---

## 3. 主机架构校对

### 3.1 会话生命周期

最终口径：`SessionController` 是会话生命周期权威入口。

| 路径 | 应有实现 | 当前状态 |
|---|---|---|
| App start | `AppCommandService -> SessionController::startSession` | 已实现 |
| App pause/resume | `SessionController` 更新 session，并 push `push_state` | 已实现并测试覆盖 |
| App end | `SessionController::endSession` 生成 report/history/stats | 已实现 |
| timer complete | `BehaviorEngine -> SessionController::endSession("timer_completed")` | 已收敛并测试覆盖 |
| away timeout | `BehaviorEngine -> SessionController::endSession("away_timeout")` | 已收敛并测试覆盖 |
| interrupted recovery | `SessionController::recoverInterruptedSessions` | 已有测试覆盖 |

仍需后续审查：

- UI 本地 END 路径是否完全复用 `SessionController`。
- 反馈写入和会话结束是否需要更强事务性。
- 报告字段是否未来统一改由 `ReportGenerator` 生成，避免双口径。

### 3.2 会话阶段

最终口径使用四阶段：

```text
idle
precheck
focusing
paused
```

校对点：

- `CALIBRATING` 是 FocusState，用于状态表达，不等于完整产品阶段模型。
- `precheck` 是会话阶段，表示 active 但尚未进入正式专注。
- App 和主机 UI 都应该读 `calibration_completed` 或 `session_phase` 判断是否进入 focusing。

### 3.3 默认时间设置

旧问题：设置页已有一个时间位置，但原来可能是离座超时，不应混用。

最终口径：

| 字段 | 含义 |
|---|---|
| `away_timeout_min` | 离座多久自动结束或处理，不是专注默认时长 |
| `default_focus_duration_min` | 首页默认专注时长 |
| 首页临时调整 | 只影响本次 start 的 target duration，不一定改默认值 |

当前代码已经有 `Preferences.default_focus_duration_min`，`get_preferences/update_preferences` 已包含并持久化该字段。

---

## 4. 视觉与行为层校对

### 4.1 产品理解

最终口径：视觉和手机上报是两路信息采集，行为层负责综合分析。

```text
视觉输入：摄像头/图片 -> VisionResult -> FocusStateMachine
手机输入：App phone_sync -> pending phone signal -> BehaviorEngine
综合上下文：BehaviorContext
策略输出：ReminderPolicy -> CommandLevel + ReminderIntent
```

### 4.2 当前验证策略

因为当前没有合适支架，摄像头链路和行为链路可以分开验证：

| 链路 | 验证目标 |
|---|---|
| 真实摄像头 | 能打开、能取帧、能进入视觉层、不崩溃 |
| 图片样本 | 用 `picture_test` 验证视觉后半段和行为状态 |
| phone_sync | 用 App 或 BLE smoke 验证手机状态进入行为层 |
| behavior -> reminder | 用本地测试和板端日志验证策略触发 |

### 4.3 手机状态等级校对

不能再写：

```text
PHONE_IN_USE = LEVEL_2
PHONE_IN_PLAYING = LEVEL_3
```

应写：

```text
PHONE_IN_USE / PHONE_IN_PLAYING 是行为状态或输入分类。
最终 CommandLevel 由 ReminderPolicy 根据 BehaviorContext、风险分、冷却、模式和偏好计算。
```

当前代码中 `ReminderPolicy` 对手机状态调用 `phoneContextLevel(behavior_context)`，符合该口径。

---

## 5. 提醒系统校对

### 5.1 silent/normal

最终口径：

```text
NORMAL/SILENT 是 channel filter，不是 severity policy。
```

对应代码：

- `NotificationRouter` 按 mode 选择 `normal_channels` 或 `silent_channels`。
- `ReminderPolicy` 中 silent 只加 message 标记，不改变 `CommandLevel`。

后续审查重点：

- App 设置 UI 是否能表达 normal/silent 下用户可启用哪些渠道。
- 主机默认 `NotificationPreferences` 是否合理。
- `push_alert` 是否包含足够信息让 App 解释提醒。

### 5.2 Policy 到 Device

旧 AGENTS 中曾标记 Policy -> Device 断链。当前应按最新代码重新理解：

```text
ReminderPolicy 生成 ReminderIntent
  -> NotificationRouter 生成 DeviceAction
  -> DeviceManager.issueAction
  -> BAND action 调 BleBandController.sendAlert
```

因此“ReminderPolicy 生成命令但从不调用 DeviceManager”这条已经不能作为当前结论直接引用。更准确的审查点是：

- dispatch 失败是否有日志和诊断字段。
- 各 channel 的执行结果是否能被 console 观察。
- 手环 BlueZ 写入失败是否能被 App 或日志定位。

---

## 6. App 协议与产品校对

### 6.1 命令集合

最终命令仍为 12 个：

```text
get_device_status
get_current_state
get_today_summary
get_last_report
session_control
update_preferences
reminder_ack
phone_sync
get_console_data
session_feedback
get_session_history
get_preferences
```

校对点：旧 10 API 口径不能再使用。

### 6.2 Push

最终 push 仍为 4 个：

```text
push_state
push_alert
push_session_end
push_timer_complete
```

当前优先级：

| push | 当前处理 |
|---|---|
| `push_state` | 核心，需要消费 |
| `push_session_end` | 核心，需要消费 |
| `push_timer_complete` | 核心，需要消费 |
| `push_alert` | 可以调查，但不能排在主链路前面 |

### 6.3 start 后页面

最终口径：

1. 点击 start 后，设备进入云台/预检页。
2. 用户手动 skip 后进入 focusing 倒计时页。
3. 若 UI 自动跳过云台页，应优先查状态同步或旧逻辑，不把它当需求。
4. 暂停后应停在暂停页，不应直接去结束页。

### 6.4 App history/report/feedback

最终口径：同一 `session_id` 必须贯穿。

重点校对：

- `end` 响应和 `push_session_end` 都不能丢 session_id。
- `get_session_history(limit=1)` 仍是当前无分片阶段的安全策略。
- `session_feedback` 应按 session_id 幂等更新。

---

## 7. 手环校对

### 7.1 当前事实

手环已经送去封装。默认只做外部联调和记录，不主动改固件。

已知实测：

| 项 | 状态 |
|---|---|
| OLED 中文 UI | 用户实测通过 |
| 充电休眠 | 用户实测通过 |
| 按键熄屏 | 用户实测通过 |
| 长按重配蓝牙 | 用户实测通过 |
| 写入 `010101` | 用户实测有响应 |

仍需最终主机侧验证：

| 项 | 验收 |
|---|---|
| 主机真实写 `01 03 03` | 手环震动/OLED 提示 |
| 断连/重连 | 恢复后还能再次提醒 |

### 7.2 禁止误写

不能写：

- 手环有 ACK。
- 手环有 Notify。
- 主机能读手环电量。
- App 直连手环。
- 手环 Text 中文是可靠协议能力。

应写：

- 主机写 `0x180C/0x2A56`。
- 当前主流程只使用 Alert `{0x01, pattern, level}`。
- 手环是否震动以实测为准，主机只能记录 BlueZ 写入结果。

---

## 8. 旧文档逐项修订建议

| 文件 | 建议处理 |
|---|---|
| `项目推进计划.md` | 不再把 App 校准 UI、push_alert UI、Dashboard 决策放在主机独立闭环之前；补充手环已封装事实 |
| `doc/01-主体文档.md` | 把“placeholder 自动完成”改成“当前应手动 skip；自动跳过按 bug 查”；补充 `SessionController` 与 `SessionPhase` |
| `doc/03-主机手机协议.md` | 补充 `already_active/session_phase/default_focus_duration_min` 当前字段；强调 phone_sync 响应不驱动 UI |
| `doc/04-手机App文档.md` | 校准 UI 不再作为当前第一阻塞项；先围绕主机核心闭环回归 |
| `doc/05-手环文档.md` | 增加手环已送封装、默认不改固件；保留只读/实测记录口径 |
| `P0-子项目任务清单.md` | 更新状态：Policy->Device 旧断链已不应作为未实现结论；新增主机独立闭环验收 |
| `doc/08-主机Todo.md` | 优先级重排：session 生命周期、report/history/feedback、dispatch 可观测性高于 UI 边角 |

本次没有直接改这些冻结/旧文档，避免破坏协议冻结历史。建议后续如要清理，新增 v2.2 文档或在文件顶部加“已被最终最终版覆盖”的说明。

---

## 9. 当前剩余风险清单

| 风险 | 影响 | 下一步 |
|---|---|---|
| 板端未跑最新重构代码 | 本地通过不等于板端通过 | 板子空出后交叉编译部署，监控日志 |
| UI 本地 END 路径可能仍有双入口 | 会话结束统计可能不一致 | 继续审查 UI -> SessionController 路径 |
| ReportGenerator 与 SessionController 报告逻辑双口径 | 后续字段可能漂移 | 设计单一 report factory 或统一调用 |
| DeviceAction 执行失败观测不足 | 演示时难定位手环未震原因 | 增加日志和 console 字段 |
| 摄像头支架缺失 | 真实视觉行为难稳定复现 | 摄像头只测取帧；行为后半用图片样本 |
| 手环封装后不可轻易改固件 | 固件问题修复成本高 | 先做 `01 03 03` 和断连重连 smoke |
| App push_alert 未消费 | 不影响主链路，但诊断体验缺一块 | 主链路过后再决定展示/忽略策略 |

---

## 10. 下一步校对后执行建议

按产品经理和架构师视角，下一步不要扩大范围，按这个顺序继续：

1. 主机代码级审查：UI 本地会话路径是否全部走 `SessionController`。
2. 主机代码级审查：report/history/feedback 是否以同一 session_id 原子闭环。
3. 主机代码级审查：ReminderPolicy -> NotificationRouter -> DeviceManager -> BleBandController 的失败日志和诊断字段。
4. 板端部署最新主机程序，跑 start -> skip -> focusing -> phone_sync -> reminder -> end -> history 的日志验证。
5. 手环封装完成后，只做主机写 `01 03 03` 和断连重连验证，不主动改手环代码。

---

## 11. 最终判定规则

后续讨论如果出现分歧，按这个顺序判定：

```text
协议冻结文件是否允许
  -> 当前主机代码是否已经实现并通过测试
  -> 是否服务于可演示主链路
  -> 是否会要求手环返工
  -> 是否只是 UI/字符/边角体验
```

只有前三项同时支持的工作，才应进入当前优先级。要求手环返工、要求改校准协议、要求 Dashboard 重构、要求手环上行能力的工作，默认后置。
