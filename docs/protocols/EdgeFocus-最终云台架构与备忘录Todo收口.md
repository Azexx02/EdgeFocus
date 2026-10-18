# EdgeFocus 最终云台架构与备忘录 Todo 收口

> 日期：2026-06-30  
> 性质：旧备忘录、旧 Todo、云台/校准相关口径的收口文档。本文不新增协议，不要求当前实现真实云台驱动，只定义边界和优先级。

---

## 1. 当前结论

云台不是当前可演示主链路的核心阻塞，但云台页需要保留。

当前可演示口径是：

```text
start session
  -> session_phase=precheck
  -> 显示云台/预检页
  -> 用户手动 skip
  -> calibration_completed=true
  -> session_phase=focusing
  -> 摄像头/视觉/手机输入/行为/提醒/报告继续主链路
```

自动跳过云台/预检页不是产品设计。如果没有用户点击 skip 却进入 focusing，应按 bug 排查。

---

## 2. 云台在产品架构中的位置

云台属于“专注会话正式开始前的环境准备模块”，不是会话权威源，也不是行为判断源。

```text
SessionController
  -> Precheck/GimbalOrchestrator
      -> GimbalDriver
      -> CameraFrameQuality
      -> optional VisionQualityGate
  -> focusing
      -> Vision + phone_sync
      -> BehaviorEngine
      -> ReminderPolicy
      -> DeviceManager
```

当前实现阶段，`Precheck/GimbalOrchestrator` 可以是轻量 placeholder，但必须有清楚边界：

- 它只影响 `calibration_completed` 和 `session_phase`。
- 它不生成 `session_id`。
- 它不决定提醒等级。
- 它不写手环。
- 它不改变 App-主机协议。
- 它不让 App 直接控制电机。

---

## 3. 当前预检页职责

当前预检页保留的原因：未来真实云台需要这个阶段，用户也需要知道正式专注还没开始。

当前页面只承担三件事：

1. 显示会话已创建，但尚未进入正式专注。
2. 允许用户手动 skip。
3. 允许用户 end 结束短会话。

当前页面不承担：

- 不做角度/位置/视野引导。
- 不做校准失败原因展示。
- 不做真实云台控制 UI。
- 不做新校准协议。
- 不把摄像头质量问题阻塞主链路演示。

---

## 4. 未来真实云台模块边界

未来要做真实云台时，应作为独立模块接入，而不是散落到 UI、BehaviorEngine 或 App 协议里。

建议模块：

| 模块 | 职责 |
|---|---|
| `GimbalOrchestrator` | 预检状态机，协调电机、摄像头质量、超时和 skip |
| `GimbalDriver` | 电机底层控制，只暴露有限动作和状态 |
| `FrameQualityEvaluator` | 判断画面是否可用于专注检测 |
| `PrecheckViewModel/UI` | 展示预检状态和 skip/end 操作 |

建议状态：

```text
idle
starting
centering
checking_frame
ready
skipped
failed
```

这些状态是主机内部状态，不应直接变成 App 必须依赖的新协议，除非后续确实需要 App 展示细分进度，并按 `P0-接口变更流程.md` 走协议变更。

---

## 5. 云台与会话生命周期关系

| 会话阶段 | 云台状态 | 行为层 | 提醒 | 统计 |
|---|---|---|---|---|
| `idle` | 不运行 | 不运行 | 不触发 | 无 |
| `precheck` | 可运行或等待 skip | 不计正式分心 | 不触发普通提醒 | 不计入正式专注质量 |
| `focusing` | 不作为主链路阻塞 | 正式运行 | 可触发 | 计入会话 |
| `paused` | 不运行 | 冻结 | 不触发 | 暂停统计 |

原则：云台只决定能否从 `precheck` 进入 `focusing`。进入 `focusing` 后，走神判断由视觉层和手机输入进入行为层完成。

---

## 6. 云台相关交互时机

### 6.1 start 后

主机返回：

```text
active=true
session_id=...
session_phase=precheck
calibration_completed=false
```

App/主机 UI 应显示云台/预检页。

### 6.2 用户 skip

用户点击 skip 后：

```text
calibration_completed=true
session_phase=focusing
focus_state=FOCUSED 或后续由行为层更新
```

App 可通过 `push_state` 或 `get_current_state` 看到状态变化。

### 6.3 用户 end

预检阶段也允许 end。结束仍走 `SessionController::endSession()`，生成短会话报告或可解释的结束记录。

### 6.4 phone_sync 与视觉

`precheck` 中：

- `phone_sync` 可以 ACK，但不计正式走神。
- 摄像头画面可以用于诊断，但不触发提醒。
- 手环不应因预检阶段手机使用直接震动。

---

## 7. 旧备忘录 Todo 收口

### 7.1 已完成或已冻结

| 项 | 当前状态 |
|---|---|
| 手环 OLED 中文 UI | 已完成并由用户实测通过 |
| 手环充电休眠 | 已由用户实测通过 |
| 手环按键熄屏 | 已由用户实测通过 |
| 手环长按重配蓝牙 | 已由用户实测通过 |
| 手环写入 `010101` 响应 | 已由用户实测通过 |
| 手环协议 `0x180C/0x2A56` Alert | 冻结，手环已送封装 |
| App-主机 FF00/FF01/FF02 基础契约 | 已整理到最终 API 契约 |
| `PHONE_IN_USE` / `PHONE_IN_PLAYING` 非固定等级 | 已校正并写入最终文档 |
| silent 不降级 | 已校正并写入最终文档 |

### 7.2 当前主链路仍需优先验证

| 项 | 目标 |
|---|---|
| start -> precheck -> 手动 skip -> focusing | 不自动跳过，不丢 session_id |
| 摄像头真实取帧 | 只验证能获取真实画面并送入视觉层 |
| 图片样本驱动视觉后半段 | 用 `picture_test` 验证粗状态 |
| phone_sync -> 行为层 | 确认 focusing 中被消费，暂停/预检不计入 |
| 行为层 -> 提醒策略 | 确认等级由策略决定，不由手机状态固定 |
| 提醒策略 -> DeviceManager | 确认命令实际执行并可观测 |
| DeviceManager -> 手环 | 手环返回后验证 `01 03 03` 和断连重连 |
| end -> report/history/feedback | 同一个 `session_id` 贯穿闭环 |

### 7.3 后置，不进入当前主链路优先级

| 项 | 后置原因 |
|---|---|
| 真实云台驱动 | 当前无支架和稳定硬件条件，先保留架构边界 |
| 校准失败原因 | 容易扩大协议和 UI 范围，当前不做 |
| 角度/位置/视野引导 | 不是当前可演示闭环阻塞 |
| App 校准细分 UI | 当前只需识别 precheck/focusing 和 skip/end |
| `push_alert` 完整 UI | 可诊断，但不排在核心链路前 |
| Dashboard 重构 | 不属于主机独立核心闭环 |
| 手环 ACK/Notify/电量上报 | 手环已封装，协议未设计上行 |
| 手环 Text 中文可靠协议 | 当前不承诺，主链路只用 Alert |

### 7.4 废弃或错误口径

| 旧口径 | 当前结论 |
|---|---|
| 云台 placeholder 自动完成可作为最终方案 | 错。自动跳过按 bug 看待 |
| App 直连手环 | 错。App 只连主机 |
| App 指定手环震动 pattern | 错。pattern 由主机策略和 `BleBandController` 映射 |
| `PHONE_IN_USE` 固定 LEVEL_2 | 错。它只是行为输入/状态 |
| `PHONE_IN_PLAYING` 固定 LEVEL_3 | 错。它只是行为输入/状态 |
| silent 降低提醒等级 | 错。silent 只筛选无声渠道 |
| UI 字符问题优先于主链路 | 错。除非阻塞演示理解，否则后置 |

---

## 8. UI 中文化口径

### 8.1 手环

手环 OLED 中文 UI 已完成并实测通过。手环已送封装，后续默认不主动修改手环 UI 文案。

手环当前仍不承诺：

- 通过 BLE Text 帧可靠显示任意中文。
- App 或主机动态推送中文长文本到手环。

主链路只依赖 Alert 震动和手环本地固定提示。

### 8.2 主机屏幕

主机屏幕 UI 中文化属于可用性优化。当前优先级低于：

1. 会话生命周期一致性。
2. 行为层与提醒链路可观测。
3. report/history/feedback 闭环。
4. 摄像头/视觉基础稳定性。

如果主机 UI 文案影响现场演示理解，可以做最小改动；不应为了字体、字符边角问题反复消耗主链路调试时间。

### 8.3 App

App UI 中文化如果只是展示文案，不应阻塞主机独立闭环。App 当前更重要的是：

- start/current/pause/resume/end 状态正确。
- precheck/focusing/paused/end 页面切换正确。
- history/report/feedback 使用主机 `session_id`。
- phone_sync 上报准确。

---

## 9. 后续 agent 使用规则

后续 agent 看到云台、校准、UI 中文化相关任务时，先按以下问题判断优先级：

1. 是否影响 start -> precheck -> skip -> focusing？
2. 是否影响 session_id、report/history/feedback 一致性？
3. 是否影响 phone_sync/vision -> behavior -> reminder？
4. 是否只是 UI 文案、字符、细节体验？
5. 是否需要改协议或返工手环？

只有 1-3 是当前主链路优先事项。4 默认后置，5 必须走 `P0-接口变更流程.md`。
