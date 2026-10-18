# App 更新交接：外部供电、Home/Go 与云台能力门

- 板端基线日期：2026-07-15
- 协议文档：`app-band-protocol-v2.1.md`
- 发布方式：新版板端与新版 App 同步升级；旧 App 不在本轮兼容范围内

## 必须删除

- 电量百分比、电池电压、“正在充电”和电池图标；
- `battery_available`、`battery_percent`、`battery_voltage_mv`、`battery_charging`；
- 旧通用 `power_source` 及其任何本地兼容映射；
- 把 `power_profile` 当作可持久化偏好的逻辑；
- 仅凭旧 `gimbal_online/gimbal_safe` 字段显示云台控制的逻辑。

## 新供电状态模型

```text
external_power_state       PRESENT | ABSENT | UNKNOWN
external_power_present     true | false | null
external_power_sample_age_ms number | null
power_profile              home | go
power_profile_source       auto_external | auto_no_external | auto_failsafe | manual
manual_override_active     boolean
```

UI 建议：

- `PRESENT`：显示“外部供电 · Home”；
- `ABSENT`：显示“移动供电 · Go”，不要写“剩余电量”；
- `UNKNOWN`：显示“供电检测不可用 · Go”；
- `manual_override_active=true`：在档位旁标注“手动”，不承诺重启后保留。

App 手动切档发送：

```json
{"cmd":"set_power_profile","id":40,"data":{"power_profile":"home"}}
```

成功响应返回当前 `power_profile`、`power_profile_source`、`manual_override_active` 和 `power_epoch`。
下一次确认的外部供电插拔会清除手动覆盖，App 以随后收到的板端状态为准。

## 会话与手机事实保持不变

- 正式会话仍由 `session_control` 启动并进入 PRECHECK；
- App 仍每 2 秒发送带递增 sequence 的 `phone_sync`；
- 类别仍为未使用、学习、娱乐和其他/未知使用；
- App 不计算最终分神等级；
- `push_alert` / `push_session_end` 继续按 event ACK 可靠处理。

## 云台 PRECHECK UI 已开放

板端正式主程序已接入经过实机验证的云台搜索、Pose 居中、构图确认、FOCUSING 跟踪和安全停机链。
App 仍必须以 `client_hello.server_capabilities` 中的 `gimbal_precheck_v1` 为唯一入口门：能力存在时显示自动搜索状态；只有 `runtime_snapshot.gimbal.manual_lease=true` 且健康未降级时才显示方向小步微调、回中、停止、重新对齐和确认位置。

每个 PRECHECK 命令必须携带当前 session id 和严格递增 sequence，不得恢复原始角度输入、串口透传、BOOT0 或远程复位。App 确认位置后再发送 `session_control.skip_precheck`；只有该提交成功才进入正式 FOCUSING。能力消失或 `runtime_snapshot.gimbal.degraded=true` 时立即禁用按钮，由板端自行低频恢复，不在 App 重试运动命令。

## 联调验收

1. 所有板端响应均不存在 `battery_*` 与 `power_source`；
2. 未接外部供电自动为 Go，接入后稳定切为 Home；
3. 手动 Home/Go 在同一供电 epoch 内保持；
4. 插拔一次后手动标志清除并恢复自动策略；
5. `UNKNOWN` 不伪装为“未充电”，显示检测不可用；
6. 蓝牙重连后用新的 `push_state/get_current_state` 完整覆盖 App 缓存；
7. 云台能力未开放时没有可点击控制入口。
