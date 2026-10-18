# 云台 PRECHECK 与 App 对接说明（2026-07-23）

## 1. 职责边界

App 只负责 PRECHECK 阶段的云台控制。

## 2. App 需要处理的 PRECHECK 状态

App 读取：

```text
runtime_snapshot.session.phase
runtime_snapshot.gimbal.state
runtime_snapshot.gimbal.search_completed
runtime_snapshot.gimbal.controls
```

只有 `session.phase == PRECHECK` 时显示 App 云台控制。

PRECHECK 使用以下权威按钮字段：

| 字段 | App 行为 |
|---|---|
| `skip_search_enabled` | 启用“跳过自动搜索” |
| `manual_enabled` | 启用方向、回中、普通步进和微调步进 |

App 不应根据固定延时猜测搜索或微调按钮状态，应以最新 PRECHECK
运行时快照为准。“停止云台”是无条件提交的安全命令，不读取、不等待
任何主机能力字段；只要 App 持有当前 `session_id` 且主机连接可发送，
按钮即可点击。

主机还会在 PRECHECK 云台状态发生变化时主动推送
`push_gimbal_precheck_state`。其 `data` 包含
`session_id/state/degraded/health_status/detail/fault_class/fault_code/search_completed/controls`。
App 收到后应立即刷新同一套按钮状态；蓝牙重连后仍以
`get_current_state` 的完整快照重新校准。

## 3. PRECHECK 页面行为

自动搜索期间：

- 显示并根据 `skip_search_enabled` 启用“跳过自动搜索”。
- “停止云台”始终可提交；点击会关闭 HOLD 和电机。
- 方向、回中、普通步进和微调步进保持禁用。

自动搜索自然完成或用户跳过后：

- `gimbal.state == manual_available`
- `gimbal.search_completed == true`
- `gimbal.controls.manual_enabled == true`
- `gimbal.controls.skip_search_enabled == false`
- `gimbal.manual_lease == false`

此时 App 自动开放方向、回中、普通步进和微调步进。

## 4. PRECHECK 命令

自动搜索期间：

- `precheck_skip_auto_adjust`
- `gimbal_stop`（强制释放）

进入 `manual_available` 后：

- `gimbal_nudge`
- `gimbal_center`
- `gimbal_stop`（强制释放）
- `precheck_confirm_position`

这些 PRECHECK 命令不携带 `lease_token`，仍需携带：

```json
{
  "session_id": "当前会话 ID",
  "owner_id": "App 稳定实例 ID",
  "sequence": 1
}
```

`sequence` 必须按同一 `owner_id` 单调递增。`gimbal_nudge` 另带：

```json
{
  "direction": "UP | DOWN | LEFT | RIGHT",
  "fine_tune": false
}
```

普通步进使用 `fine_tune=false`，微调使用 `fine_tune=true`；具体角度由主机配置决定。

App 的 `gimbal_stop` 不执行位置保持。命令成功后主机进入 `released`，关闭 HOLD 和电机，不再继续自动搜索或自动恢复。

发生故障时主机会先自行执行安全停机，App 无需等待用户点击：

- 故障发生在 PRECHECK：仍处于 PRECHECK 时可低频自动恢复；如果用户在降级状态下直接开始正式会话，则本次会话不再自动恢复云台。
- 故障发生在正式会话：主机低频恢复成功后回到故障前保存的位置，进入 HOLD，并恢复视觉自动追踪。
- App 点击“停止云台”属于用户明确强制释放，无论此前故障发生在哪个阶段，都不再自动恢复。

## 5. 页面退出与会话切换

出现以下任一情况时，App 关闭 PRECHECK 云台控制区并禁用全部按钮：

- PRECHECK 结束。
- 会话结束或取消。
- `session_id` 改变。
- 主机断开。
- 云台进入故障或降级状态时禁用搜索、方向和回中；“停止云台”不依赖
  `controls`，连接仍可发送时继续保留。

不要新增或依赖 `gimbal_async_motion_v1`、`gimbal_manual_step_v1` 等主机未声明的能力字段。
