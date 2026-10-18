# App ↔ 主机云台会话协议 3

## 能力门

`client_hello.data.server_capabilities` 必须同时包含：

- `gimbal_session_control_v3`
- `gimbal_manual_lease_v1`
- `gimbal_async_result_v1`

缺任一项时，App 将云台显示为只读/不可控制；不得回退到旧角度或 UART 命令。

## 命令生命周期

```text
App command(sequence) → 主机响应 queued=true
                      → 主机单 worker 串行执行
                      → G431 ACK（firmware_accepted=true）
                      → POSE 同 generation 到位（settled=true，可选）
                      → push_gimbal_command_result（可靠事件）
                      → App event_ack
```

主机队列最多 32 项。`gimbal_stop` 与 `gimbal_force_release` 插入队首；执行后较旧 sequence 的运动命令按幂等规则拒绝。

## 请求公共字段

```json
{
  "cmd":"gimbal_nudge",
  "id":120,
  "data":{
    "session_id":"session_...",
    "owner_id":"android-installation-uuid",
    "lease_token":"...",
    "sequence":17,
    "direction":"LEFT"
  }
}
```

- `session_id`：必须等于当前权威会话。
- `owner_id`：App 安装级稳定 ID；同一会话中不变。
- `sequence`：每 owner 严格递增，且不得为 0。
- `lease_token`：接管最终结果签发；keepalive、nudge、center、stop、release、force-release 使用。
- `direction`：仅 `UP|DOWN|LEFT|RIGHT`，步长由主机配置决定。

`gimbal_manual_takeover` 不带 token；`gimbal_force_release` 另带 `confirmed=true`。`gimbal_recover` 和 `precheck_skip_auto_adjust` 不要求 owner/token，但仍需 session/sequence。

## 入队响应与最终事件

入队成功：

```json
{"status":"success","id":120,"data":{"queued":true,"command":"gimbal_nudge","request_sequence":17,"session_id":"session_..."}}
```

最终可靠事件：

```json
{
  "cmd":"push_gimbal_command_result",
  "event_id":"evt_...",
  "data":{
    "command":"gimbal_nudge",
    "request_id":120,
    "request_sequence":17,
    "session_id":"session_...",
    "owner_id":"android-installation-uuid",
    "accepted":true,
    "firmware_accepted":true,
    "settled":false,
    "motion_generation":41,
    "acked_through_sequence":17
  }
}
```

接管成功事件还包含 `lease_token/lease_epoch/lease_expires_at`。App 先持久化/去重，再对该 `event_id` 发送 `event_ack`；BLE 写成功或 `queued=true` 均不能让 UI 显示“已到位”。

## 租约和恢复

- 接管后每 5 秒 `gimbal_lease_keepalive`。
- token 不匹配、过期或 session 变化：立即清空本地 token、停止发送方向命令，重新读取状态。
- PRECHECK 人工控制不污染正式分神推理；进入正式会话后允许人工接管时，自动跟踪暂停，释放后恢复。
- 故障时按钮禁用；`gimbal_recover` 只做恢复，不重复已经完成的自动搜索。
- 会话结束后主机返回会话构图原点并 STOP；App 清空 token 和 sequence scope。

## 维护隔离

电气校准、机械原点、电流阈值和 NVM 清除绝不通过 BLE/App 暴露。它们只允许工程人员使用 `gimbal_bringup` 或主机 `/maintenance` 隐藏入口，并受无会话、STOP、限时解锁和二次确认保护。
