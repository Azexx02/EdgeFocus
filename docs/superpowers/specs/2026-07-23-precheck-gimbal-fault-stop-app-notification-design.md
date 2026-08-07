# PRECHECK 云台异常停机与 App 通知设计

日期：2026-07-23

## 目标

PRECHECK 云台异常时：

1. 主机不等待 App，立即执行安全停机。
2. App 主动收到异常状态变化。
3. App 的“停止云台”仍可用，其语义固定为强制释放。
4. 重复停止安全且幂等。

## 方案比较

### A. 主机自动停机 + App 通知与冗余停止（采用）

主机故障检测是安全主链；App 停止是人工冗余链。蓝牙断连时仍安全，App 在线时可以显示故障并再次确认停机。

### B. 只由 App 发停止

实现简单，但依赖蓝牙连接、UI 刷新和用户响应，不能作为硬件安全链。

### C. 只由主机停机

能够保证安全，但 App 无法及时显示异常，也无法提供用户明确的停止操作。

## 状态机

```text
PRECHECK 正常状态
    │ 检测到 Fault / RecoveryCheck / 非法硬件状态 / 运动失败
    ▼
主机 stopAndDisable
    │
    ├─ 成功或硬件已经 Disabled ─► DEGRADED（保留原始故障原因）
    └─ 失败 ───────────────────► DEGRADED（保留故障 + 停机失败信息）

DEGRADED
    │ 主机按现有低频策略尝试恢复
    │
    ├─ 恢复成功 ─► 搜索或手动可用状态
    └─ App 发送 gimbal_stop ─► RELEASED（关闭 HOLD/电机，禁止自动恢复）
```

## 主机安全停机

- `degradeLocked()` 第一次进入异常时立即调用 `stopAndDisable`。
- 调用失败仍保持 `DEGRADED`，不得伪装成已安全停止。
- 自动恢复保持现有低频节奏，不高速重试。
- 主机安全停机不依赖 App 是否在线。

## App 主动通知

`SessionSnapshotPublisher` 为云台快照提供状态变化回调。`AppCommandService` 只在 PRECHECK 且下列字段发生变化时发送一次 `push_gimbal_precheck_state`：

- `state`
- `degraded`
- `health_status`
- `detail`
- `fault_class`
- `fault_code`
- `search_completed`
- `controls.manual_enabled`
- `controls.skip_search_enabled`

不按每个视觉帧推送，避免蓝牙拥塞。App 仍可通过 `get_current_state` 获取同一份权威状态。

## App 停止命令

- App 发送现有 `gimbal_stop`，无需新增命令名。
- “停止云台”不进行能力协商，也不等待任何 `controls` 字段；App
  只要持有当前 `session_id` 且连接可发送就立即提交。
- App 的 `gimbal_stop` 在所有 PRECHECK 状态下都固定表示强制释放，不表示 HOLD。
- `gimbal_stop` 绕过正常手动调整状态校验，直接执行幂等 `stopAndDisable`。
- 成功后进入 `RELEASED`，表示用户明确要求保持停机，不再自动恢复。
- 同一 `owner_id + sequence` 重放返回原结果，不重复运动。
- 已经 Disabled/Released 时新的停止命令也返回成功。

## 网页 PRECHECK 控制

网页不再提供含混的普通“停止”按钮，只保留“强制释放”：

- `强制释放`：执行 `stopAndDisable`，关闭 HOLD 和电机，允许手动转动云台。

网页 PRECHECK 与 FOCUSING 的“强制释放”均不依赖手动接管权。它与
App 的“停止云台”调用同一条幂等强制释放后端。

## App 产品字段

App 只使用 PRECHECK 控制字段：

```json
{
  "manual_enabled": false,
  "skip_search_enabled": false
}
```

不向 App 产品契约暴露停止能力字段；停止是无条件提交的安全命令。
也不向 App 暴露其他阶段的控制字段。

## 验收

1. 控制器进入 Fault 后立即调用 `stopAndDisable`，状态仍为 `DEGRADED`。
2. App 收到一次 `push_gimbal_precheck_state`，其中 `degraded=true`，且不包含停止能力字段。
3. App `gimbal_stop` 在正常或异常 PRECHECK 均执行强制释放并进入 `RELEASED`。
4. 重复停止成功且不重复发送硬件运动。
5. 网页不含普通“停止”；“强制释放”关闭 HOLD/电机。
6. 正常 PRECHECK 的跳过和微调行为不回归。

## 2026-07-23 恢复边界补充

- 控制器记录第一次进入 `DEGRADED` 时的 phase。
- PRECHECK 中发生的故障只在仍处于 PRECHECK 时允许低频恢复；若用户在该降级状态下开始正式会话，则保持安全禁能。
- 正式会话中发生的故障在停机前缓存实际 yaw/pitch。恢复成功后回到缓存位置、进入 HOLD，并由现有视觉跟踪循环恢复自动追踪；绝不重新进入 PRECHECK。
- 用户强制释放进入 `RELEASED`，覆盖上述自动恢复规则。
