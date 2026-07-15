# EdgeFocus App 主机适配迭代设计

- 日期：2026-07-16
- 状态：用户已确认，待规格审查
- App 仓库：`C:\Users\Aze\Desktop\APP\EdgeFocusApp`
- 主机联调基线：`C:\Users\Aze\Desktop\self_developed\code\focus_app`
- 改造前 App 基线提交：`ff073ab Checkpoint EdgeFocusApp before host alignment iteration`
- 首个真机目标：Redmi Note 10 Pro，MIUI 14.0.8，Android 13

## 1. 目标

将现有 Android App 对齐当前 EdgeFocus 主机 `schema_version=2.1`，完成 LOCAL 与 EDGEFOCUS 双模式、可靠事件、离线补传、PRECHECK、提醒、屏保、手环代理、供电状态和历史数据的产品收束。

本轮持续实施到完整验收。实现可按依赖顺序分阶段验证，但阶段之间不等待用户批准；只有硬件、权限或主机能力形成真实阻塞时才报告。

## 2. 规格来源与优先级

实现以以下内容为准：

1. `C:\Users\Aze\Desktop\APP\新app迭代必看\app-host-redesign-handoff-2026-07-16.md`
2. `C:\Users\Aze\Desktop\APP\新app迭代必看\app-host-alignment-v3.md`
3. `C:\Users\Aze\Desktop\APP\新app迭代必看\app-target-architecture.md`
4. 主机 `docs/integration/app-band-protocol-v2.1.md`
5. 主机当前工作树 `network/AppCommandService.cpp`

冲突时，当前主机工作树的真实命令与响应优先于旧 App 模型。不得从现有 `BoardJsonParser` 的默认值反推协议。

## 3. 已确认的产品决策

### 3.1 会话模式

- 保留 LOCAL 与 EDGEFOCUS。
- 模式由现有设置开关选择，不增加每次开始前的模式弹窗。
- 会话开始后 owner 冻结。
- LOCAL 中途连接主机不迁移为 EDGEFOCUS，只补传已经结束的 LOCAL 记录。
- EDGEFOCUS 未 READY 时禁止开始，不自动降级 LOCAL。
- LOCAL 完全离线可运行。

### 3.2 NFC 与横屏控制面板

- 保留贴 NFC 打开 `ControlPanelActivity` 的流程。
- NFC 只切换到独立横屏控制面板，不改变 LOCAL/EDGEFOCUS 设置和当前会话 owner。
- 已有会话时，横屏继续展示同一 `session_id` 和 phase。
- 横屏控制面板不是首页的横屏版本。
- 横屏继续使用独立三页 Pager：
  1. 专注控制；PRECHECK 时在第一页原位显示预检；
  2. 本次会话统计；
  3. 控制面板设置。
- 第二、三页不是首页“数据”“设置”页面的横屏副本。
- 第一页保留现有圆形计时器、停止图标、播放/暂停图标等主要视觉与操作结构。

### 3.3 UI 风格

- 保留现有浅色视觉、圆形计时器、导航习惯和大致布局。
- 不整体换肤，不改造成深色高密度控制台。
- 首页与横屏第一页只做必要状态接线、错误提示和过期字段清理，不做明显视觉重构。
- 正常状态不额外显示“主机权威”“状态已同步”。
- 仅在断线、状态陈旧、命令确认中或错误时显示必要提示。

### 3.4 开发期数据

- App 仍在最终版开发阶段，不要求迁移现有开发数据库中的旧会话。
- 可以提升 Room 版本并使用一次性破坏性升级或显式清库初始化。
- 最终新结构内部仍必须保证 Session、Event Inbox、Sync Outbox 的事务一致性与重启恢复。

## 4. 架构

采用模块化迁移，不在现有 `TimerStateHolder` 与 `BleManager` 中继续堆叠职责，也不整体重写 UI。

### 4.1 单一状态源

新增进程级 `@Singleton AppSessionCoordinator`，向所有页面暴露不可变 `AppUiSnapshot`。

建议状态：

```text
AppUiSnapshot
├─ selectedMode: LOCAL | EDGEFOCUS
├─ connection: DISCONNECTED | CONNECTING | SUBSCRIBED | NEGOTIATING | READY | DEGRADED
├─ session
│  ├─ owner: APP | HOST
│  ├─ phase: UNKNOWN | IDLE | PRECHECK | RUNNING | PAUSED | ENDING | FINISHED
│  ├─ sessionId
│  ├─ targetMs / remainingMs
│  ├─ stale / pendingAction
│  └─ result
├─ precheck
├─ phoneFact
├─ alertQueue
├─ band
├─ power
└─ sync
```

页面 ViewModel 只负责：

- 将 `AppUiSnapshot` 映射为页面文案和可用性；
- 转发 Start/Pause/Resume/End/AckAlert/PrecheckAction 等 intent；
- 保存 Pager 页码等纯页面状态。

页面不得自行保存会话、启停手机监控、完成主机会话或执行断线恢复。

### 4.2 组件边界

```text
data/ble/
  BleTransport            GATT、MTU、notification、单包写入
  BleChunkCodec           response chunk 与 push_chunk 完整重组
  BleRequestScheduler     请求 ID、单写入、优先级、超时、取消
  ConnectionSupervisor    退避重连、订阅、hello、状态重建

data/session/
  SessionRepository       统一会话缓存与查询
  SessionSyncOutbox       LOCAL 可靠补传
  HostEventInbox          关键事件去重与 ACK 阶段

domain/session/
  AppSessionCoordinator   唯一协调器
  LocalSessionRuntime     LOCAL 权威状态机
  HostSessionClient       EDGEFOCUS 命令与主机快照

domain/phone/
  PhoneFactMonitor        原始手机事实与权限/TTL 语义

domain/alert/
  AlertDeliveryCoordinator 前台覆盖、声音、通知、振动、用户确认

domain/band/
  BandProxyClient         主机代理手环状态与 capability gate

presentation/shared/
  PrecheckPanel
  AlertOverlay
  BlackScreenSaver
  ConnectionStatus
  BandStatusPanel
```

现有 Debug BLE 模拟与日志功能保留，但只能出现在 Debug 构建。

## 5. 双模式状态机

### 5.1 LOCAL

```text
IDLE -> RUNNING <-> PAUSED -> FINISHED -> IDLE
```

- App 生成稳定 UUID。
- App 负责计时、手机事实消费、本地提醒和结果生成。
- 连接变化不影响当前会话。
- 结束时在同一 Room 事务写完整 Session、Sync Outbox，并设置 `synced=false`。
- 后续连接 READY、App 启动或重连触发补传。

### 5.2 EDGEFOCUS

```text
IDLE -> PRECHECK -> RUNNING <-> PAUSED -> ENDING -> FINISHED -> IDLE
```

- App 的 start/pause/resume/end 只是 intent。
- 只有主机响应或权威快照能改变 phase。
- App 固定将主机 wire phase `FOCUSING` 映射为 UI/domain `RUNNING`。
- 主机当前没有持久 `FINISHED` phase；App 只有在收到匹配的 `push_session_end`、end 成功报告，或 `IDLE` 加匹配报告时才进入 FINISHED，禁止单凭 `IDLE` 完成落库。
- 快照缺少 session_id/phase 等当前状态必需字段或 phase 无法识别时，拒绝整份新快照，保留上一份值并标记 `stale=true`、`phase=UNKNOWN`；不得用 IDLE 或旧数值枚举补齐。
- 本地倒计时仅用于显示插值，不改变 phase。
- 本地剩余时间到零时显示等待主机确认，不本地完成。
- 只有 `push_session_end`、end 成功报告，或权威 FINISHED/IDLE 加匹配报告，才能保存主机会话结果。
- start/pause/resume/end 超时表示结果未知；重连后查状态，不盲目重复命令。

### 5.3 Activity 与页面唯一性

- `MainActivity` 与 `ControlPanelActivity` 消费同一 coordinator。
- 贴 NFC、返回、旋转、Activity 重建和 Pager 切页不得生成第二个 session、重复启动监控或重复落库。
- 横屏退出后返回竖屏仍显示同一会话。

## 6. 连接、协商与恢复

连接完整阶段：

```text
DISCONNECTED -> CONNECTING -> SUBSCRIBED -> NEGOTIATING -> READY
```

READY 条件：

1. GATT 已连接；
2. RX notification 已订阅；
3. `client_hello` 成功，记录 `schema_version=2.1` 与 server capabilities；
4. `get_current_state` 成功并覆盖连接前缓存；
5. 建立本地 `connection_epoch`。

`client_hello` 前到达的 `push_state` 可临时缓存，但不能提前进入 READY。当前 schema 2.1 没有 `boot_id/state_revision`，不能把它们作为 READY 硬门槛；重连后强制全量读状态即可。

恢复规则：

- 自动重连采用有上限退避；设置页允许主动重试主机连接。
- LOCAL 会话不受连接变化影响。
- EDGEFOCUS 断线保留最后快照并标记 stale，暂时禁用控制动作。
- PRECHECK、RUNNING、PAUSED、ENDING 均支持恢复。
- 重连后重新订阅、hello、全量读状态，以主机覆盖 App 缓存。
- 不按 App 旧 phase 回写主机。

## 7. BLE 请求调度与协议语义

同一 GATT 连接内只允许一个写入中的请求，并按以下优先级调度：

```text
P0 event_ack / session_control
P1 phone_sync heartbeat
P2 client_hello / get_current_state / band_control
P3 preferences / history
P4 camera preview
```

- 预览请求可取消，不得阻塞 ACK、会话控制或 2 秒心跳。
- 请求 ID 在当前连接内唯一。
- 所有响应和 push 必须先完成分片重组，再解析或 ACK。
- `status=success` 不等于业务成功，必须检查 `success`、`accepted`、`stored` 等业务字段。
- 超时只表示结果未知。
- 未知字段忽略；缺失必需字段进入显式 UNKNOWN。
- 不使用默认 67%、旧电量或旧状态补齐 UI。
- `session_control(action=start)` 固定发送 `source_mode=live_camera`，不得发送主机展示页的预录或前端演示模式。

## 8. 手机原始事实

`PhoneFactMonitor` 只生产事实，不计算主机最终分神等级。

事实映射：

- 未使用：`is_in_use=false`；
- 学习：`is_in_use=true, usage_state=learning`；
- 娱乐：`is_in_use=true, usage_state=playing`；
- 其他/未知使用：`is_in_use=true, usage_state=using`。

EDGEFOCUS 时序：

- PRECHECK 不发送；
- RUNNING 首帧立即发送；
- RUNNING/PAUSED 每 2 秒发送新 sequence；
- 断线停止发送；
- 重连确认同一会话后 sequence 继续递增并立即补当前事实；
- 新会话 sequence 从 1 开始；
- 检查 `accepted` 与 `last_accepted_sequence`。

sequence checkpoint：

- 为当前正式 `session_id` 持久化最近发送和最近被主机接受的 sequence。
- App 进程重启或连接恢复后，从 `runtime_snapshot.phone.sequence`、本地持久 checkpoint 和最近响应的 `last_accepted_sequence` 取最大值，下一次发送使用 `max(...) + 1`。
- 只有权威快照确认进入新的 `session_id` 后才从 1 开始。
- 主机拒绝某 sequence 时不复用冲突值；更新 checkpoint 后用更大的下一 sequence 发送当前真实事实。

禁止发送或消费为最终判定：

- `distraction_level`；
- `phone_distraction_level`；
- App 自算 risk/focus_state。

前台语义：

- `MainActivity`、`ControlPanelActivity`、二者切换窗口、App 黑屏屏保和系统屏幕关闭/设备非交互，均视为没有使用手机做无关事项；在需要发送心跳时明确发送 `is_in_use=false`，不得用 `learning` 伪装。
- 缺少 Usage Access 时进入本地 `UNKNOWN_PERMISSION` 并提示授权。schema 2.1 没有 UNKNOWN wire 值，因此不发送 `phone_sync`，让主机 5 秒 freshness 自然过期。
- UsageEvents 短暂空洞只在 TTL 内沿用最近可信事实；TTL 过期后进入 UNKNOWN 并停止发送，直到重新获得可信事实。
- “稳定 2 秒心跳”适用于存在可信事实的 RUNNING/PAUSED；UNKNOWN_PERMISSION/UNKNOWN 是明确例外，不制造虚假事实。

## 9. 可靠事件与提醒交付

可靠事件：

- `push_alert`
- `push_session_end`

处理顺序：

```text
完整重组
-> Room 事务写 HostEventInbox，按 event_id 去重
-> event_ack(received)
-> 前台效果或系统通知实际投递
-> event_ack(presented)
-> 仅 push_alert 的用户主动确认
-> event_ack(user_ack)
```

- ACK 成功状态从响应 `data.ack_status` 读取，不读取不存在的顶层 accepted 字段。
- ACK 阶段只能前进。
- `accepted` 和 `duplicate` 视为阶段成功。
- `session_mismatch/unknown_event/expired` 记录诊断，不伪装成功。
- 同一 event_id 不重复播放声音、闪屏或振动。
- 多提醒使用持久队列，不再使用单一 `lastAlert` 覆盖槽。
- 旧 `reminder_ack` 不进入正式链路。

事件类型矩阵：

| 事件 | received | presented | user_ack |
|---|---|---|---|
| `push_alert` | 入库后发送 | 前台效果开始或通知实际投递后发送 | 用户主动确认提醒后发送 |
| `push_session_end` | 结果/事件入库后发送 | 结束结果页面或通知实际展示后发送 | 本轮永不发送，避免错误触发提醒冷却 |

TTL 与崩溃语义：

- 使用主机可靠事件中的 `created_at_ms`、`ttl_ms`、`remaining_ttl_ms`；`push_alert` 当前投递 TTL 为 10 秒，`push_session_end` 为 60 秒。
- 每次准备投递以及 App 重启恢复时都重新检查 TTL；过期事件保留诊断但不再展示、播放或振动。
- 外部提醒效果采用 at-most-once：先在事务中把事件标记为 `dispatch_claimed/effect_started`，再调用声音、振动或通知 API。
- 通知 ID 由 event_id 稳定派生，重放只更新同一通知。
- 如果在 `effect_started` 落库后、实际效果确认前崩溃，恢复时将其标为 `delivery_uncertain`，不重复声音/振动，也不伪发 `presented`；可保留或恢复同一稳定 ID 的静态通知。
- Event Inbox 分别保存 received/presented/user_ack 的本地与主机确认状态，未确认阶段可重试 ACK，但不重复外部效果。

交付方式：

- App 任意前台页面：全局提醒层、SCREEN 卡片/闪烁、SOUND 一次性播放；
- App 后台或系统锁屏：高优先级通知与振动；
- 通知点击回到当前会话；
- SCREEN 提醒可唤醒 App 黑屏屏保的视觉层。

## 10. LOCAL Outbox 与历史

LOCAL 结束事务同时写：

1. 完整 Session；
2. `upload_local_session` Outbox；
3. `synced=false`。

补传规则：

- 按 `session_id` 幂等；
- 主机返回 `stored=true` 或 `already_exists=true` 均视为成功；
- 成功后同一事务标记 `synced=true` 并保存主机确认信息；
- 不依赖用户打开数据页；
- 不只在会话结束瞬间尝试；
- 重启 App 后仍能继续补传。

历史同步为进程级仓库任务：

- EDGEFOCUS 记录以主机版本覆盖缓存；
- LOCAL 上传前以 App 结果为准；
- 同一 session_id 不重复计入统计；
- 数据页显示来源、同步状态和结果完整性；
- 反馈只保留 `too_much / too_little / just_right`。

## 11. PRECHECK 与预览

竖屏专注页和横屏第一页共享 PRECHECK 业务组件，但按各自布局展示。

- 显示真实低带宽预览图，不只显示元数据。
- 解码主机 `data_base64` 并绘制像素/JPEG。
- 显示时间戳、sequence、质量和陈旧提示。
- PRECHECK 期间以约 1–2 FPS 的可取消 P4 请求刷新。
- 离开 PRECHECK、断线或进入 RUNNING 后立即停止拉取。
- 显示相机、人物、距离、姿态和自动搜索状态。
- 主机不允许跳过时隐藏或禁用“跳过”。

云台仅在 `gimbal_precheck_v1` 存在时显示。命令必须带当前 `session_id` 和严格递增 sequence，只允许方向、center、stop、realign、confirm 等产品命令；不得暴露步长、角度、UART、BOOT0 或复位。

精确命令与业务判定：

| UI 意图 | 主机 cmd | 必需 data | 成功条件 |
|---|---|---|---|
| 停止自动搜索 | `precheck_skip_auto_adjust` | session_id, sequence | response success 且 data.accepted=true |
| 方向微调 | `gimbal_nudge` | session_id, sequence, direction=UP/DOWN/LEFT/RIGHT | response success 且 data.accepted=true |
| 居中 | `gimbal_center` | session_id, sequence | response success 且 data.accepted=true |
| 停止运动 | `gimbal_stop` | session_id, sequence | response success 且 data.accepted=true |
| 重新对齐 | `gimbal_realign` | session_id, sequence | response success 且 data.accepted=true |
| 确认位置 | `precheck_confirm_position` | session_id, sequence | response success 且 data.accepted=true |

- 每次尝试使用严格递增 sequence；业务拒绝或超时后不得复用旧 sequence。
- “停止自动搜索”不等于跳过 PRECHECK。
- 确认进入正式会话必须执行两步序列：`precheck_confirm_position` 成功后，再发送 `session_control(action=skip_precheck)`；只有随后主机快照进入 `FOCUSING` 才把 App phase 映射为 RUNNING。
- 如果快照不允许 App 跳过、manual lease 不存在或 capability 消失，相关按钮隐藏或禁用。

## 12. 手环代理与供电

### 12.1 手环

- App 不直接连接手环。
- 当前主机无 `band_proxy_control_v1` 时，只读显示 `bracelet_connected` 和已有诊断。
- 搜索/重连按钮禁用，并说明当前主机仅支持自动重连。
- capability 存在后才调用 `get_band_status` 与 `band_control(scan_connect|reconnect)`。
- `accepted=true` 只表示任务已接受，不表示已连接。
- 竖屏设置页与横屏第三页复用同一手环状态组件。

### 12.2 外部供电与 Home/Go

使用：

- `external_power_state: PRESENT | ABSENT | UNKNOWN`
- `external_power_present`
- `external_power_sample_age_ms`
- `power_profile: home | go`
- `power_profile_source`
- `manual_override_active`

手动切换只发送 `set_power_profile`，仅对当前供电 epoch 有效，不保存为 App 长期偏好。

删除所有电量百分比、充电状态、旧 `power_source` 和默认 67%。

## 13. 屏保与 Android 生命周期

- `MainActivity` 和 `ControlPanelActivity` 前台统一设置 `FLAG_KEEP_SCREEN_ON`。
- 黑屏屏保是 App 内纯黑覆盖层，不是系统锁屏。
- 空闲时长可配置，建议默认 2 分钟，提供 30 秒、1/2/5/10 分钟。
- 首次触摸只退出黑屏，不透传底层按钮。
- 屏保不停止 BLE、会话、协调器或提醒。
- App 后台不伪造黑屏前台，改用系统通知。

MIUI 14 / Android 13 真机重点验证：

- POST_NOTIFICATIONS 权限；
- connectedDevice/specialUse 前台服务要求；
- MIUI 后台限制与自启动行为；
- Activity 防自动锁屏；
- 黑屏屏保和系统非交互不会制造手机分心事实。

## 14. UI 页面收束

### 14.1 首页

- 保留现有计时器与主要操作布局。
- EDGEFOCUS 未 READY 时禁用开始并显示原因。
- 主机 start 后原地进入 PRECHECK。
- 会话中显示必要 phase、剩余时间、提醒和错误，不显示旧 risk/focus_state 数字。

### 14.2 横屏控制面板

- 保留原独立三页 Pager 和图标式控制。
- Page 1：专注控制或 PRECHECK。
- Page 2：本次会话统计与结果完整性。
- Page 3：App 提醒表现、手环代理、Home/Go、主机连接和屏保入口。
- 删除电量、困倦、板端屏幕/声音和手机最终等级。

### 14.3 设置页

- 模式开关；
- 主机绑定、连接状态、主动重连；
- 手环代理状态及 capability-gated 搜索/重连；
- 应用分类；
- 屏保开关和等待时长；
- App 声音/屏幕提醒设置。

### 14.4 数据页

- 统一本地与主机记录；
- 显示来源、同步状态、完整性；
- 证据不足或提前结束不显示“全程专注”；
- 历史同步不依赖页面生命周期。

## 15. 删除项

必须删除新产品链路中的：

- 电量百分比、充电状态、默认 67%、旧 `power_source`；
- 困倦、疲劳、建议休息状态和文案；
- 板载屏幕、板载声音及其路由设置；
- `phone_distraction_level`、手机最终等级和 App 自算 risk/focus_state；
- 正式链路中的 `reminder_ack`；
- 页面 ViewModel 各自保存会话、启停监控和断线补偿；
- 无生产调用且不能完成可靠重连的旧 `BleForegroundService` 实现。

主机仍可能返回遗留字段。App 可忽略解析以避免崩溃，但不得显示、写回或用作状态机输入。

## 16. 错误处理与诊断

- 命令错误分为传输失败、超时未知、协议解析错误、业务拒绝和 capability 不支持。
- 不把 BLE 写成功当业务成功。
- 不用 UI 本地改色模拟命令成功。
- capability 缺失不是 BLE 故障。
- 状态必需字段缺失进入 UNKNOWN，不填旧默认值。
- 所有联调失败记录：request ID、session ID、event ID、connection epoch、主机响应、App 本地 snapshot 和时间。
- 日志避免记录不必要的完整隐私数据或无限增长的大图 payload。

## 17. 测试策略

### 17.1 单元测试

- 协议模型与 JSON 解析；
- chunk response 与 push_chunk 重组；
- RequestScheduler 优先级、取消和超时；
- schema 2.1 hello 与 capability gate；
- 手机事实映射、sequence、TTL 和自 App 前台判定；
- phone sequence 持久 checkpoint、进程重启恢复与拒绝后递增；
- UNKNOWN 与遗留字段忽略；
- LOCAL 结果与反馈规则。

### 17.2 集成与重启测试

至少包含：

- `AppSessionCoordinator` 双模式集成测试；
- PRECHECK/RUNNING/PAUSED/ENDING 断线恢复测试；
- Event Inbox 重放、阶段恢复和重启去重测试；
- Event Inbox 在 claim 前、effect_started 后、外部效果后各崩溃边界的恢复测试；
- 事件 TTL 到期、过期重放与稳定 notification ID 测试；
- Sync Outbox 离线结束、App 重启、重复上传测试；
- 竖横屏共享 session_id、Activity 重建不重复落库测试。

### 17.3 UI 与设备测试

- 首页主要布局不发生明显回归；
- 横屏独立三页 Pager、图标控制和 PRECHECK；
- 全局提醒覆盖不同前台页面；
- 后台通知、振动和通知点击；
- 黑屏屏保首次触摸拦截；
- 手环 capability 有/无；
- PRESENT/ABSENT/UNKNOWN 与 Home/Go。

关键协议用例必须写成可判定断言：

- `event_ack` 响应读取 `data.ack_status`；覆盖 accepted、duplicate、session_mismatch、unknown_event、expired。
- `push_alert` 产生 received → presented → user_ack；`push_session_end` 最多产生 received → presented，抓包中没有 user_ack。
- 运行中杀 App 后恢复同一 session，下一 phone sequence 大于本地 checkpoint、runtime snapshot 和 last accepted 的最大值。
- MainActivity → ControlPanelActivity 切换、App 黑屏和系统非交互抓包均为 `is_in_use=false`。
- Usage Access 缺失或事实 TTL 过期时抓包中停止 phone_sync，而不是发送 learning/using 占位。
- start 请求固定包含 `source_mode=live_camera`。
- PRECHECK 确认抓包顺序为 `precheck_confirm_position` 成功后 `session_control(skip_precheck)`，最终等待 `FOCUSING` 快照。

### 17.4 真机联调顺序

1. 连接、notification、hello、get_current_state；
2. EDGEFOCUS start → PRECHECK → preview → RUNNING → pause/resume/end；
3. phone_sync 未使用/学习/娱乐/其他与 2 秒心跳；
4. push_alert 前台、黑屏、后台、重放、用户确认；
5. push_session_end 自动结束、手动结束、断线恢复；
6. LOCAL 离线结束、重启、连接、重复上传；
7. power PRESENT/ABSENT/UNKNOWN、Home/Go、插拔清除覆盖；
8. gimbal capability 无/有、故障、恢复；
9. band 先验证只读，再验证 `band_proxy_control_v1`。

## 18. 实施顺序

1. 新协议模型、BLE transport/codec/scheduler、连接协商；
2. 新 Room 结构、Event Inbox 与 Sync Outbox；
3. 唯一 `AppSessionCoordinator` 与双模式状态机；
4. 手机事实心跳、自 App 前台和屏保语义；
5. 全局提醒交付；
6. 竖横屏共享 PRECHECK 与真实预览；
7. 手环代理与 Home/Go；
8. 页面接入新 snapshot，保留首页和横屏第一页主要 UI；
9. 删除旧字段、旧链路和重复职责；
10. 自动化测试、真机联调、问题修复与回归。

## 19. 完成定义

交付必须同时满足：

1. 当前主机 schema 2.1 完成真机 hello、状态读取和会话控制；
2. 不依赖 boot_id/revision 才能 READY；
3. 关键事件完成持久化去重与三阶段 ACK；
4. phone_sync 无 distraction_level，稳定 2 秒心跳；
5. 竖横屏 PRECHECK 显示真实预览；
6. LOCAL 离线会话重启后可幂等补传；
7. 竖横屏共享 session_id/phase，不重复落库；
8. 横屏仍是原三页独立控制面板且保留图标控制；
9. `ControlPanelActivity` 不被手机事实监控判为外部 App；
10. 手环按钮严格受 `band_proxy_control_v1` 控制；
11. 不显示电量、困倦、板端输出或手机最终等级；
12. App 前台不自动锁屏，黑屏屏保不触发手机分心；
13. PRECHECK/RUNNING/PAUSED 断线后正确恢复且不本地误完成；
14. coordinator、断线恢复、Inbox/Outbox 重启测试通过；
15. Redmi Note 10 Pro / MIUI 14 / Android 13 完成通知、前台服务、屏保和 BLE 真机回归。
