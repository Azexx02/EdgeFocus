# EdgeFocus 最终最终版主链路文档

> 日期：2026-06-30  
> 适用范围：主机 `C:\Users\Aze\Desktop\BoardCode`、App `C:\Users\Aze\Desktop\APP\EdgeFocusApp`、手环 `C:\Users\Aze\Desktop\BLEband\stm32\STM32Band_test`  
> 性质：当前项目实际口径的收口文档，用于后续开发、联调、验收。协议冻结文件仍以 `P0-协议冻结清单.md` 为准，本文件不替代协议冻结流程。

---

## 1. 当前产品目标

EdgeFocus 的当前核心不是云台、不是校准细节、不是 Dashboard 重构，而是先形成一个可真实演示、可被代码和日志解释的专注会话闭环。

最低可演示闭环为：

```text
App 或主机发起会话
  -> 主机创建并接管 session_id
  -> 用户手动跳过云台/预检页进入专注
  -> 摄像头采集或图片样本进入视觉层
  -> App 上报 phone_sync 作为手机使用输入
  -> 行为层综合视觉和手机输入
  -> 提醒策略决定是否提醒、提醒等级和提醒渠道
  -> 主机执行本机提醒或写手环 Alert
  -> 会话结束
  -> 主机生成报告、写入历史
  -> App 查看历史/报告并提交反馈
```

主机是这个闭环的权威端。App 是操作与展示端，也是手机使用状态采集端。手环是提醒终端，不参与业务判断。

---

## 2. 三端职责冻结

| 端 | 当前职责 | 不是它负责的事 |
|---|---|---|
| 主机 | 会话管理、视觉输入、行为判断、提醒策略、设备执行、报告和历史、App BLE GATT Server、手环 BLE Central | 不把最终等级交给 App 决定；不依赖 App 作为权威数据源 |
| App | 发起/控制会话、显示状态、同步历史、提交反馈、上报手机前台应用状态 | 不直连手环；不决定手环震动 pattern；不把 `phone_sync` 响应当作最终状态 |
| 手环 | 接收主机写入的 Alert 并震动/OLED 提示 | 不上报电量；不 ACK；不 Notify；不参与策略判断 |

数据优先级固定为：

```text
主机 > App > 手环
```

如果 App 本地缓存与主机历史冲突，以主机为准。如果手环是否真实震动不可读，以主机 BlueZ 写入诊断和实测现象共同判断。

---

## 3. 主机内部产品架构

主机应用应被理解为一个“类番茄钟 + 多源走神采集 + 行为判定 + 提醒执行 + 报告反馈”的产品，而不是单纯的摄像头 demo。

### 3.1 主链路模块

```text
SessionController
  -> SystemStateStore
  -> CameraPipeline / VisionEngine / InferenceScheduler
  -> BehaviorEngine / FocusStateMachine / EventDetector / RiskScorer
  -> ReminderPolicy / NotificationRouter
  -> DeviceManager / ScreenController / BleBandController
  -> SQLiteStore / ReportGenerator
  -> BleGattServer / AppCommandService
```

### 3.2 模块职责

| 模块 | 产品职责 | 当前代码口径 |
|---|---|---|
| `SessionController` | 会话生命周期入口 | start/pause/resume/end，end 负责 report、today stats、SQLite completed session |
| `SystemStateStore` | 权威内存状态中心 | 保存 session、device、reminder、frame_quality、preferences、behavior_context、push callback |
| `AppCommandService` | App 命令语义层 | 处理 12 个普通命令；start/current/pause/resume/end/history/feedback/preferences 已走主流程 |
| `BleGattServer` | BLE GATT 通道 | FF00/FF01/FF02；普通响应按 id；push 走 `cmd=push_*` |
| `BehaviorEngine` | 行为层入口 | 消费 phone_sync 缓冲和视觉结果；更新 FocusState；处理 timer/away 自动结束 |
| `FocusStateMachine` | 行为状态防抖 | 视觉与手机信号进入状态机后得到 FocusState |
| `ReminderPolicy` | 是否提醒与提醒等级 | 根据 FocusState + BehaviorContext + cooldown 决策；手机状态不是固定等级 |
| `NotificationRouter` | 提醒渠道筛选 | NORMAL 选 normal channels，SILENT 选 silent channels；不改变严重程度 |
| `DeviceManager` | 设备执行分发 | 执行 BAND/SCREEN/SOUND/LIGHT action；BAND 调用 `BleBandController::sendAlert` |
| `BleBandController` | 手环 BLE Central | BlueZ D-Bus 连接手环并写 `0x2A56` Alert |
| `SQLiteStore` | 持久化 | 会话、事件、偏好、反馈、历史 |
| `ReportGenerator` | 报告生成能力 | 存在且测试覆盖；当前生产 end 路径主要由 `SessionController` 汇总报告字段 |

---

## 4. 会话生命周期最终口径

### 4.1 会话阶段

当前主机内部阶段以 `SessionPhase` 表达：

| phase | 条件 | 产品含义 |
|---|---|---|
| `idle` | `active=false` | 无活动会话，首页可选默认时长 |
| `precheck` | `active=true && paused=false && calibration_completed=false` | 云台/预检页，当前保留页面，用户可手动 skip |
| `focusing` | `active=true && paused=false && calibration_completed=true` | 正式专注倒计时，行为层开始处理手机和视觉输入 |
| `paused` | `active=true && paused=true` | 暂停，行为层冻结，不处理 phone_sync 为分心 |

### 4.2 发起会话

入口可以是 App，也可以是主机屏幕。无论入口是谁，主机必须创建唯一 `session_id` 并成为权威。

App start 响应必须让 App 能知道当前是否正在会话、是否还在预检页：

```text
success
session_id
already_active
session_phase
remaining_ms
target_duration_ms
focus_state
calibration_completed
```

若未传时长，主机使用 `preferences.default_focus_duration_min`，默认 25 分钟。设置页中的离座超时不是默认专注时长，两者必须分开。

### 4.3 云台/预检页

云台页保留，因为未来仍是需要的功能。当前没有推进真实云台驱动，也不修改校准协议。

当前可演示口径是：

1. start 后进入 `precheck`。
2. UI 显示云台/预检页。
3. 用户手动点击 skip。
4. 主机将 `calibration_completed=true`。
5. 进入 `focusing` 倒计时页。

不能再写成“云台 placeholder 自动完成是最终方案”。自动跳过如果出现，应按 bug 处理，而不是产品设计。

### 4.4 暂停和恢复

暂停后：

- session 仍 active。
- `paused=true`。
- App 和主机 UI 都应显示 paused。
- 行为层丢弃暂停期间的视觉帧和 phone_sync，不累计分心。
- pause/resume 要 push `push_state`，让 App 状态即时刷新。

恢复后：

- `paused=false`。
- session_id 不变。
- 继续原会话倒计时。

### 4.5 结束会话

所有生产结束路径都应尽量收敛到 `SessionController::endSession()`：

| 结束原因 | 当前目标口径 |
|---|---|
| App 手动 end | `AppCommandService` 调用 `SessionController::endSession()` |
| 主机本地 END | UI 路径调用同一会话控制入口 |
| 倒计时结束 | `BehaviorEngine` 检测 remaining <= 0 后调用 `SessionController::endSession("timer_completed")` |
| 离座超时 | `BehaviorEngine` 检测 away timeout 后调用 `SessionController::endSession("away_timeout")` |
| 程序恢复 | `SessionController::recoverInterruptedSessions()` 关闭旧 open session |

结束后必须同时满足：

- 内存 `session.active=false`。
- `last_report.session_id` 等于该会话。
- today stats 增加。
- SQLite session 被标记 completed。
- App 可通过 push 或 history/report 看到同一个 session_id。

---

## 5. 视觉与行为综合分析

视觉层和手机上报是两路走神信息采集，二者都不是最终提醒等级。

### 5.1 视觉输入

真实摄像头当前只需要先验证：

1. 能打开真实摄像头。
2. 能获取画面。
3. 画面能进入视觉层并产出 `VisionResult`。
4. `frame_quality` 能写入 `SystemStateStore`，可通过 console 观察。

由于当前没有合适支架，视觉后半段可先使用图片样本目录：

```text
C:\Users\Aze\Desktop\BoardCode\picture_test
```

此阶段不追求算法完美，只覆盖粗场景：

```text
FOCUSED
AWAY
LOOKING_AWAY
PHONE_IN_USE
PHONE_IN_PLAYING
```

### 5.2 手机输入

App 通过 `phone_sync` 上报手机前台使用情况。主机把它写入 `SystemStateStore` 的 pending phone signal，`BehaviorEngine` 在 focusing 阶段消费。

重要口径：

- `PHONE_IN_USE` 不是固定 `LEVEL_2`。
- `PHONE_IN_PLAYING` 不是固定 `LEVEL_3`。
- `phone_sync.distraction_level` 是输入强度，不是最终 `CommandLevel`。
- 学习类 App 对应 `PHONE_IN_LEARNING`，默认不提醒。
- 暂停和预检阶段不应把 phone_sync 计入走神。

### 5.3 行为层综合

行为层的责任是把视觉结果和手机信号转成当前 FocusState 和 BehaviorContext。BehaviorContext 至少包含：

```text
risk_score
vision_signal
phone_signal
phone_distraction_level
phone_usage_state
reason
```

ReminderPolicy 再根据 FocusState 和 BehaviorContext 决定是否提醒及等级。

---

## 6. 提醒策略与渠道

### 6.1 等级决策

`CommandLevel` 是主机策略层最终输出：

| level | 名称 | 含义 |
|---|---|---|
| 0 | NONE | 不提醒 |
| 1 | LEVEL_1 | 轻提醒 |
| 2 | LEVEL_2 | 中提醒 |
| 3 | LEVEL_3 | 强提醒 |
| 4 | REST_SUGGESTION | 休息建议 |

ReminderPolicy 需要遵守：

- `FOCUSED`、`PAUSED`、`CALIBRATING`、`PHONE_IN_LEARNING` 不触发普通提醒。
- 手机类状态使用 BehaviorContext 推导等级，不固定映射。
- `LOW_CONFIDENCE` 更偏诊断和引导，不应轻易强提醒。
- cooldown 生效，避免连续轰炸。

### 6.2 NORMAL 与 SILENT

NORMAL 和 SILENT 是提醒渠道筛选，不是严重程度降级。

| 模式 | 作用 |
|---|---|
| NORMAL | 使用用户勾选的 normal channels，例如屏幕、声音、灯光、手环 |
| SILENT | 使用用户勾选的 silent channels，例如手环、屏幕静默提示 |

silent 模式不能把 `LEVEL_3` 降成 `LEVEL_2`，也不能因为静音而改变是否走神。它只改变“用什么方式提醒”。

### 6.3 设备执行

```text
ReminderPolicy
  -> ReminderIntent
  -> NotificationRouter.route(mode, preferences)
  -> DeviceManager.issueAction(action)
  -> BAND: BleBandController.sendAlert(level)
  -> SCREEN/SOUND/LIGHT: 本机提示路径
```

当前手环写入 Alert：

```text
{0x01, pattern, level}
```

当前 pattern 映射：

```text
level >= 3 -> pattern 0x03
level >= 2 -> pattern 0x02
else       -> pattern 0x01
```

手环封装后不主动改固件。后续主机侧只应保持兼容 `0x180C/0x2A56` Alert 写入。

---

## 7. App 与主机协议实际口径

协议仍为 FF00/FF01/FF02：

```text
FF00 Service
FF01 App -> 主机 JSON Write
FF02 主机 -> App JSON Read/Notify
```

普通命令为 12 个：

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

Push 为 4 个：

```text
push_state
push_alert
push_session_end
push_timer_complete
```

当前代码级改进口径：

- 普通命令异常不能让主机崩溃，应返回 `status:error`。
- pause/resume 必须返回完整 session state 并 push `push_state`。
- start 遇到已有会话返回当前会话和 `already_active=true`，不重复创建。
- `get_preferences` 包含 `default_focus_duration_min`，用于首页默认专注时长。
- `phone_sync` 响应只作为 ACK/调试，App UI 以 push 或 `get_current_state` 为准。

---

## 8. 手环当前状态

手环已经送去封装，后续默认不主动大改。

继承实测事实：

- OLED 中文 UI 已通过用户实测。
- 充电休眠已通过用户实测。
- 按键熄屏已通过用户实测。
- 长按重配蓝牙已通过用户实测。
- 写入 `010101` 有响应。
- 需要在主机侧最终验证真实写 `01 03 03` 是否震动，以及断连/重连后是否恢复。

当前不能承诺：

- 手环 ACK。
- 手环 Notify。
- 手环状态读取。
- 手环电量上报。

主机和 App 文档不得再把这些写成已有能力。

---

## 9. 会话报告、历史与反馈

会话报告和历史必须围绕同一个 `session_id` 贯穿：

```text
start.session_id
  == current.session_id
  == end.report.session_id
  == SQLite completed session_id
  == get_session_history.sessions[].session_id
  == session_feedback.session_id
```

报告核心字段：

```text
session_id
total_duration
focus_duration
distraction_count
reminder_count
phone_distraction_count
```

反馈核心值：

```text
too_much
too_little
just_right
```

当前产品建议：反馈主要放在 App，因为主机屏幕较小，不适合做复杂反馈交互。主机负责接收并持久化反馈。

---

## 10. 当前不推进事项

以下事项不进入当前主链路优先级：

| 事项 | 当前处理 |
|---|---|
| 云台真实驱动 | 保留页面和未来接口，不在本轮推进 |
| 校准协议修改 | 不改 |
| 校准失败原因 | 不做 |
| 位置/角度/视野引导 | 不做 |
| Dashboard 重构 | 不做主线阻塞项 |
| 手环 ACK/Notify/电量上报 | 不做，除非后续重新开协议变更 |
| push_alert UI 优先开发 | 可调查，但不能排在核心链路前 |
| 字符/UI 细节反复调试 | 避免消耗主链路时间 |

---

## 11. 当前验收口径

### 11.1 主机独立闭环

主机不依赖 App 也应能完成：

1. 本机启动会话。
2. 进入预检页。
3. 手动 skip。
4. 进入 focusing 倒计时。
5. 摄像头或图片样本产生视觉输入。
6. 行为层更新状态。
7. 提醒策略产生命令。
8. 设备层执行提醒。
9. 结束会话。
10. 写入报告和历史。

### 11.2 App 联动闭环

App 需要完成：

1. 连接主机。
2. start/current/pause/resume/end。
3. phone_sync 上报。
4. 看到 push_state / push_session_end / push_timer_complete。
5. get_session_history。
6. session_feedback。
7. get/update preferences，包括默认专注时长。

### 11.3 手环提醒闭环

手环封装后只做外部联调验证：

1. 主机连接 `EdgefocusBand`。
2. 主机找到 `0x180C/0x2A56`。
3. 主机写 `01 03 03`。
4. 手环实际震动/OLED 提示。
5. 断连/重连后仍可再次提醒。

---

## 12. 近期执行顺序

当前最合理的后续顺序：

1. 主机本地继续代码级审查：确认 session 生命周期单入口、report/history/feedback 原子性、提醒分发失败可观测。
2. 等板子空出后部署主机，监控日志跑主机独立闭环。
3. 用 `picture_test` 做视觉后半链路测试，摄像头只验证真实画面进入视觉层。
4. App 跑 start/current/pause/resume/end/history/feedback/preferences 回归。
5. 手环封装完成后跑 `01 03 03` 和断连重连提醒 smoke。
6. 三端合并跑 3 分钟演示闭环。

---

## 13. 文档完成定义

后续任何总结文档，如果与本文件冲突，按以下优先级判定：

```text
P0 协议冻结清单
  > 当前代码和测试事实
  > 本最终最终版主链路文档
  > 旧推进计划/旧 Todo/旧总结
```

旧文档中出现以下表述时，应按本文件校正：

- “App 直连手环”。错误。
- “手环有 ACK/Notify/电量上报”。错误。
- “PHONE_IN_PLAYING 固定 LEVEL_3”。错误。
- “silent 会降低提醒等级”。错误。
- “云台 placeholder 自动完成是最终方案”。错误。
- “Dashboard 重构是当前主链路前置条件”。错误。
- “校准失败原因、角度引导是当前优先事项”。错误。
